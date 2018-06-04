#include "node_processor.h"
#include "../utility/serialize.h"
#include "../core/block_crypt.h"
#include "../core/serialization_adapters.h"
#include "../utility/logger.h"
#include "../utility/logger_checkpoints.h"

namespace beam {

void NodeProcessor::OnCorrupted()
{
	throw std::runtime_error("node data corrupted");
}

NodeProcessor::Horizon::Horizon()
	:m_Branching(-1)
	,m_Schwarzschild(-1)
{
}

struct NodeProcessor::UnspentWalker
	:public NodeDB::WalkerSpendable
{
	NodeProcessor& m_This;

	UnspentWalker(NodeProcessor& me, bool bWithSignature)
		:m_This(me)
		,NodeDB::WalkerSpendable(me.m_DB, bWithSignature)
	{
	}

	bool Traverse();

	virtual bool OnUtxo(const UtxoTree::Key&) = 0;
	virtual bool OnKernel(const Merkle::Hash&) = 0;
};

bool NodeProcessor::UnspentWalker::Traverse()
{
	for (m_Rs.m_DB.EnumUnpsent(*this); MoveNext(); )
	{
		assert(m_nUnspentCount);
		if (!m_Key.n)
			m_This.OnCorrupted();

		uint8_t nType = *(uint8_t*) m_Key.p;
		((uint8_t*&) m_Key.p)++;
		m_Key.n--;

		switch (nType)
		{
		case DbType::Utxo:
		{
			if (UtxoTree::Key::s_Bytes != m_Key.n)
				m_This.OnCorrupted();

			static_assert(sizeof(UtxoTree::Key) == UtxoTree::Key::s_Bytes, "");

			if (!OnUtxo(*(UtxoTree::Key*) m_Key.p))
				return false;
		}
		break;

		case DbType::Kernel:
		{
			if (sizeof(Merkle::Hash) != m_Key.n)
				m_This.OnCorrupted();

			if (!OnKernel(*(Merkle::Hash*) m_Key.p))
				return false;

		}
		break;

		default:
			m_This.OnCorrupted();
		}
	}
}

void NodeProcessor::Initialize(const char* szPath)
{
	m_DB.Open(szPath);

	// Load all th 'live' data
	{
		struct Walker
			:public UnspentWalker
		{
			Walker(NodeProcessor& me) :UnspentWalker(me, false) {}

			virtual bool OnUtxo(const UtxoTree::Key& key) override
			{
				UtxoTree::Cursor cu;
				bool bCreate = true;

				m_This.m_Utxos.Find(cu, key, bCreate)->m_Value.m_Count = m_nUnspentCount;
				assert(bCreate);

				return true;
			}

			virtual bool OnKernel(const Merkle::Hash& key) override
			{
				RadixHashOnlyTree::Cursor cu;
				bool bCreate = true;

				m_This.m_Kernels.Find(cu, key, bCreate);
				assert(bCreate);

				return true;
			}
		};

		Walker wlk(*this);
		wlk.Traverse();
	}

	NodeDB::Transaction t(m_DB);
	TryGoUp();
	t.Commit();
}

void NodeProcessor::EnumCongestions()
{
	NodeDB::StateID sidPos;
	m_DB.get_Cursor(sidPos);

	// request all potentially missing data
	NodeDB::WalkerState ws(m_DB);
	for (m_DB.EnumTips(ws); ws.MoveNext(); )
	{
		NodeDB::StateID& sid = ws.m_Sid; // alias
		if (NodeDB::StateFlags::Reachable & m_DB.GetStateFlags(sid.m_Row))
			continue;

		if (sid.m_Height < sidPos.m_Height)
			continue; // not interested in tips behind the current cursor

		bool bBlock = true;

		while (sid.m_Height > Block::Rules::HeightGenesis)
		{
			NodeDB::StateID sidThis = sid;
			if (!m_DB.get_Prev(sid))
			{
				bBlock = false;
				break;
			}

			if (NodeDB::StateFlags::Reachable & m_DB.GetStateFlags(sid.m_Row))
			{
				sid = sidThis;
				break;
			}
		}

		Block::SystemState::Full s;
		m_DB.get_State(sid.m_Row, s);

		Block::SystemState::ID id;

		if (bBlock)
			s.get_ID(id);
		else
		{
			id.m_Height = s.m_Height - 1;
			id.m_Hash = s.m_Prev;
		}

		PeerID peer;
		bool bPeer = m_DB.get_Peer(sid.m_Row, peer);

		RequestData(id, bBlock, bPeer ? &peer : NULL);
	}
}

void NodeProcessor::TryGoUp()
{
	bool bDirty = false;

	while (true)
	{
		NodeDB::StateID sidPos, sidTrg;
		m_DB.get_Cursor(sidPos);

		{
			NodeDB::WalkerState ws(m_DB);
			m_DB.EnumFunctionalTips(ws);

			if (!ws.MoveNext())
			{
				assert(!sidPos.m_Row);
				break; // nowhere to go
			}
			sidTrg = ws.m_Sid;
		}

		assert(sidTrg.m_Height >= sidPos.m_Height);
		if (sidTrg.m_Height == sidPos.m_Height)
			break; // already at maximum height (though maybe at different tip)

		// Calculate the path
		std::vector<uint64_t> vPath;
		while (sidTrg.m_Row != sidPos.m_Row)
		{
			assert(sidTrg.m_Row);
			vPath.push_back(sidTrg.m_Row);

			if (sidPos.m_Height == sidTrg.m_Height)
			{
				Rollback(sidPos);
				bDirty = true;

				if (!m_DB.get_Prev(sidPos))
					sidPos.SetNull();
			}

			if (!m_DB.get_Prev(sidTrg))
				sidTrg.SetNull();
		}

		bool bPathOk = true;

		for (size_t i = vPath.size(); i--; )
		{
			sidPos.m_Height++;
			sidPos.m_Row = vPath[i];

			bDirty = true;
			if (!GoForward(sidPos))
			{
				bPathOk = false;
				break;
			}
		}

		if (bPathOk)
			break; // at position
	}

	if (bDirty)
	{
		NodeDB::StateID sidPos;
		m_DB.get_Cursor(sidPos);
		PruneOld(sidPos.m_Height);

		OnNewState();
	}
}

void NodeProcessor::PruneOld(Height h)
{
	if (h <= m_Horizon.m_Branching)
		return;
	h -= m_Horizon.m_Branching;

	while (true)
	{
		uint64_t rowid;
		{
			NodeDB::WalkerState ws(m_DB);
			m_DB.EnumTips(ws);
			if (!ws.MoveNext())
				break;
			if (ws.m_Sid.m_Height >= h)
				break;

			rowid = ws.m_Sid.m_Row;
		}

		do
		{
			if (!m_DB.DeleteState(rowid, rowid))
				break;
		} while (rowid);
	}

	if (m_Horizon.m_Schwarzschild <= m_Horizon.m_Branching)
		return;

	Height hExtra = m_Horizon.m_Schwarzschild - m_Horizon.m_Branching;
	if (h <= hExtra)
		return;
	h -= hExtra;

	for (Height hFossil = m_DB.ParamIntGetDef(NodeDB::ParamID::FossilHeight); hFossil < h; )
	{
		uint64_t rowid;

		{
			NodeDB::WalkerState ws(m_DB);
			m_DB.EnumStatesAt(ws, hFossil + Block::Rules::HeightGenesis);
			if (!ws.MoveNext())
				OnCorrupted();

			rowid = ws.m_Sid.m_Row;

			if (ws.MoveNext())
			{
				if (!hFossil)
					break; // several genesis blocks. Currently not blocked.

				OnCorrupted();
			}
		}

		assert(m_DB.GetStateFlags(rowid) & NodeDB::StateFlags::Active);

		if (1 != m_DB.GetStateNextCount(rowid))
			break;

		DereferenceFossilBlock(rowid);

		m_DB.DelStateBlock(rowid);
		m_DB.set_Peer(rowid, NULL);

		++hFossil;
		m_DB.ParamSet(NodeDB::ParamID::FossilHeight, &hFossil, NULL);
	}
}

void NodeProcessor::get_CurrentLive(Merkle::Hash& hv)
{
	m_Utxos.get_Hash(hv);

	Merkle::Hash hv2;
	m_Kernels.get_Hash(hv2);

	Merkle::Interpret(hv, hv2, true);
}

struct NodeProcessor::RollbackData
{
	// helper structures for rollback
	struct Utxo {
		Height m_Maturity; // the extra info we need to restore an UTXO, in addition to the Input.
	};

	ByteBuffer m_Buf;
	Utxo* m_pUtxo;

	Utxo* get_BufAs() const
	{
		assert(!m_Buf.empty());
		return (Utxo*) &m_Buf.at(0);
	}

	size_t get_Utxos() const
	{
		return m_Buf.empty() ? 0 : (m_pUtxo - get_BufAs());
	}

	void Prepare(const TxBase& tx)
	{
		if (!tx.m_vInputs.empty())
		{
			size_t nInputs = get_Utxos();
			m_Buf.resize(m_Buf.size() + sizeof(Utxo) * tx.m_vInputs.size());
			m_pUtxo = get_BufAs() + nInputs;
		}
	}

	void Unprepare(const TxBase& tx)
	{
		m_Buf.resize(m_Buf.size() - sizeof(Utxo) * tx.m_vInputs.size());
	}
};

bool NodeProcessor::HandleBlock(const NodeDB::StateID& sid, bool bFwd)
{
	ByteBuffer bb;
	RollbackData rbData;
	m_DB.GetStateBlock(sid.m_Row, bb, rbData.m_Buf);

	Block::SystemState::Full s;
	m_DB.get_State(sid.m_Row, s); // need it for logging anyway

	Block::SystemState::ID id;
	s.get_ID(id);

	Block::Body block;
	try {

		Deserializer der;
		der.reset(bb.empty() ? NULL : &bb.at(0), bb.size());
		der & block;
	}
	catch (const std::exception&) {
		LOG_WARNING() << id << " Block deserialization failed";
		return false;
	}

	bb.clear();

	bool bFirstTime = false;

	if (bFwd)
	{
		size_t n = std::max(size_t(1), block.m_vInputs.size() * sizeof(RollbackData::Utxo));
		if (rbData.m_Buf.size() != n)
		{
			bFirstTime = true;

			uint8_t nDifficulty = get_NextDifficulty();
			if (nDifficulty != s.m_PoW.m_Difficulty)
			{
				LOG_WARNING() << id << " Difficulty expected=" << uint32_t(nDifficulty) << ", actual=" << uint32_t(s.m_PoW.m_Difficulty);
				return false;
			}

			if (s.m_TimeStamp <= get_MovingMedian())
			{
				LOG_WARNING() << id << " Timestamp inconsistent wrt median";
				return false;
			}

			Merkle::Hash hvHist;
			if (sid.m_Height > Block::Rules::HeightGenesis)
			{
				NodeDB::StateID sidPrev = sid;
				verify(m_DB.get_Prev(sidPrev));
				m_DB.get_PredictedStatesHash(hvHist, sidPrev);
			}
			else
				ZeroObject(hvHist);

			if (s.m_History != hvHist)
			{
				LOG_WARNING() << id << " Header History mismatch";
				return false; // The state (even the header) is formed incorrectly!
			}

			if (!VerifyBlock(block, sid.m_Height, sid.m_Height))
			{
				LOG_WARNING() << id << " context-free verification failed";
				return false;
			}

			rbData.m_Buf.resize(n);
		}
	} else
		assert(!rbData.m_Buf.empty());


	rbData.m_pUtxo = rbData.get_BufAs();
	if (!bFwd)
		rbData.m_pUtxo += block.m_vInputs.size();

	bool bOk = HandleValidatedTx(block, sid.m_Height, bFwd, rbData);
	if (!bOk)
		LOG_WARNING() << id << " invalid in its context";

	if (bFirstTime && bOk)
	{
		// check the validity of state description.
		Merkle::Hash hv;
		get_CurrentLive(hv);

		if (s.m_LiveObjects != hv)
		{
			LOG_WARNING() << id << " Header LiveObjects mismatch";
			bOk = false;
		}

		if (bOk)
			m_DB.SetStateRollback(sid.m_Row, rbData.m_Buf);
		else
			HandleValidatedTx(block, sid.m_Height, false, rbData);
	}

	if (bOk)
	{
		ECC::Scalar kOffset;
		NodeDB::Blob blob(kOffset.m_Value.m_pData, sizeof(kOffset.m_Value.m_pData));

		if (!m_DB.ParamGet(NodeDB::ParamID::StateExtra, NULL, &blob))
			kOffset.m_Value = ECC::Zero;

		ECC::Scalar::Native k(kOffset), k2(block.m_Offset);
		if (!bFwd)
			k2 = -k2;

		k += k2;
		kOffset = k;

		m_DB.ParamSet(NodeDB::ParamID::StateExtra, NULL, &blob);

		AmountBig subsidy;
		subsidy.Lo = m_DB.ParamIntGetDef(NodeDB::ParamID::SubsidyLo);
		subsidy.Hi = m_DB.ParamIntGetDef(NodeDB::ParamID::SubsidyHi);

		if (bFwd)
			subsidy += block.m_Subsidy;
		else
			subsidy -= block.m_Subsidy;

		m_DB.ParamSet(NodeDB::ParamID::SubsidyLo, &subsidy.Lo, NULL);
		m_DB.ParamSet(NodeDB::ParamID::SubsidyHi, &subsidy.Hi, NULL);

		LOG_INFO() << id << " Block interpreted. Fwd=" << bFwd;
	}

	return bOk;
}

bool NodeProcessor::HandleValidatedTx(const TxBase& tx, Height h, bool bFwd, RollbackData& rbData)
{
	size_t nInp = 0, nOut = 0, nKrnInp = 0, nKrnOut = 0;

	bool bOk = true;
	if (bFwd)
	{
		for ( ; nInp < tx.m_vInputs.size(); nInp++)
			if (!HandleBlockElement(*tx.m_vInputs[nInp], bFwd, h, rbData))
			{
				bOk = false;
				break;
			}
	} else
	{
		nInp = tx.m_vInputs.size();
		nOut = tx.m_vOutputs.size();
		nKrnInp = tx.m_vKernelsInput.size();
		nKrnOut = tx.m_vKernelsOutput.size();
	}

	if (bFwd && bOk)
	{
		for ( ; nOut < tx.m_vOutputs.size(); nOut++)
			if (!HandleBlockElement(*tx.m_vOutputs[nOut], h, bFwd))
			{
				bOk = false;
				break;
			}
	}

	if (bFwd && bOk)
	{
		for ( ; nKrnInp < tx.m_vKernelsInput.size(); nKrnInp++)
			if (!HandleBlockElement(*tx.m_vKernelsInput[nKrnInp], bFwd, true))
			{
				bOk = false;
				break;
			}
	}

	if (bFwd && bOk)
	{
		for (; nKrnOut < tx.m_vKernelsOutput.size(); nKrnOut++)
			if (!HandleBlockElement(*tx.m_vKernelsOutput[nKrnOut], bFwd, false))
			{
				bOk = false;
				break;
			}
	}

	if (!(bFwd && bOk))
	{
		// Rollback all the changes. Must succeed!
		while (nKrnOut--)
			HandleBlockElement(*tx.m_vKernelsOutput[nKrnOut], false, false);

		while (nKrnInp--)
			HandleBlockElement(*tx.m_vKernelsInput[nKrnInp], false, true);

		while (nOut--)
			HandleBlockElement(*tx.m_vOutputs[nOut], h, false);

		while (nInp--)
			HandleBlockElement(*tx.m_vInputs[nInp], false, h, rbData);
	}

	return bOk;
}

template <typename T, uint8_t nType>
struct SpendableKey
{
	uint8_t m_Type;
	T m_Key;

	SpendableKey()
		:m_Type(nType)
	{
		static_assert(sizeof(*this) == sizeof(m_Type) + sizeof(m_Key), "");
	}
};

bool NodeProcessor::HandleBlockElement(const Input& v, bool bFwd, Height h, RollbackData& rbData)
{
	SpendableKey<UtxoTree::Key, DbType::Utxo> skey;

	UtxoTree::Cursor cu;
	UtxoTree::MyLeaf* p;
	UtxoTree::Key::Data d;
	d.m_Commitment = v.m_Commitment;

	if (bFwd)
	{
		struct Traveler :public UtxoTree::ITraveler {
			virtual bool OnLeaf(const RadixTree::Leaf& x) override {
				return false; // stop iteration
			}
		} t;


		UtxoTree::Key kMin, kMax;

		d.m_Maturity = 0;
		kMin = d;
		d.m_Maturity = h;
		kMax = d;

		t.m_pCu = &cu;
		t.m_pBound[0] = kMin.m_pArr;
		t.m_pBound[1] = kMax.m_pArr;

		if (m_Utxos.Traverse(t))
			return false;

		p = &(UtxoTree::MyLeaf&) cu.get_Leaf();

		skey.m_Key = p->m_Key;
		d = skey.m_Key;
		assert(d.m_Commitment == v.m_Commitment);
		assert(d.m_Maturity <= h);

		assert(p->m_Value.m_Count); // we don't store zeroes

		if (!--p->m_Value.m_Count)
			m_Utxos.Delete(cu);
		else
			cu.Invalidate();

		rbData.m_pUtxo->m_Maturity = d.m_Maturity;
		rbData.m_pUtxo++;

	} else
	{
		const RollbackData::Utxo& x = *--rbData.m_pUtxo;
		d.m_Maturity = x.m_Maturity;
		skey.m_Key = d;

		bool bCreate = true;
		p = m_Utxos.Find(cu, skey.m_Key, bCreate);

		if (bCreate)
			p->m_Value.m_Count = 1;
		else
		{
			p->m_Value.m_Count++;
			cu.Invalidate();
		}
	}

	m_DB.ModifySpendable(NodeDB::Blob(&skey, sizeof(skey)), 0, bFwd ? -1 : 1);
	return true;
}

struct NodeProcessor::UtxoSig
{
	bool	m_Coinbase;
	Height	m_Incubation; // # of blocks before it's mature

	std::unique_ptr<ECC::RangeProof::Confidential>	m_pConfidential;
	std::unique_ptr<ECC::RangeProof::Public>		m_pPublic;

	template <typename Archive>
	void serialize(Archive& ar)
	{
		ar
			& m_Coinbase
			& m_Incubation
			& m_pConfidential
			& m_pPublic;
	}
};

bool NodeProcessor::HandleBlockElement(const Output& v, Height h, bool bFwd)
{
	UtxoTree::Key::Data d;
	d.m_Commitment = v.m_Commitment;
	d.m_Maturity = h;
	d.m_Maturity += v.m_Coinbase ? Block::Rules::MaturityCoinbase : Block::Rules::MaturityStd;

	// add explicit incubation offset, beware of overflow attack (to cheat on maturity settings)
	Height hSum = d.m_Maturity + v.m_Incubation;
	d.m_Maturity = (d.m_Maturity <= hSum) ? hSum : Height(-1);

	SpendableKey<UtxoTree::Key, DbType::Utxo> skey;
	skey.m_Key = d;
	NodeDB::Blob blob(&skey, sizeof(skey));

	UtxoTree::Cursor cu;
	bool bCreate = true;
	UtxoTree::MyLeaf* p = m_Utxos.Find(cu, skey.m_Key, bCreate);

	cu.Invalidate();

	if (bFwd)
	{
		if (bCreate)
		{
			p->m_Value.m_Count = 1;

			Serializer ser;

			{
				UtxoSig sig;
				sig.m_Coinbase = v.m_Coinbase;
				sig.m_Incubation = v.m_Incubation;
				sig.m_pConfidential.swap(((Output&) v).m_pConfidential);
				sig.m_pPublic.swap(((Output&)v).m_pPublic);

				ser & sig;

				sig.m_pConfidential.swap(((Output&)v).m_pConfidential);
				sig.m_pPublic.swap(((Output&)v).m_pPublic);
			}


			SerializeBuffer sb = ser.buffer();

			m_DB.AddSpendable(blob, NodeDB::Blob(sb.first, (uint32_t) sb.second), 1, 1);
		}
		else
		{
			p->m_Value.m_Count++;
			m_DB.ModifySpendable(blob, 1, 1);
		}
	} else
	{
		if (1 == p->m_Value.m_Count)
			m_Utxos.Delete(cu);
		else
			p->m_Value.m_Count--;

		m_DB.ModifySpendable(blob, -1, -1);
	}

	return true;
}

bool NodeProcessor::HandleBlockElement(const TxKernel& v, bool bFwd, bool bIsInput)
{
	bool bAdd = (bFwd != bIsInput);

	SpendableKey<Merkle::Hash, DbType::Kernel> skey;
	v.get_HashTotal(skey.m_Key);

	RadixHashOnlyTree::Cursor cu;
	bool bCreate = bAdd;
	RadixHashOnlyTree::MyLeaf* p = m_Kernels.Find(cu, skey.m_Key, bCreate);

	if (bAdd)
	{
		if (!bCreate)
			return false; // attempt to use the same exactly kernel twice. This should be banned!
	} else
	{
		if (!p)
			return false; // no such a kernel

		m_Kernels.Delete(cu);
	}

	NodeDB::Blob blob(&skey, sizeof(skey));

	if (bIsInput)
		m_DB.ModifySpendable(blob, 0, bFwd ? -1 : 1);
	else
		if (bFwd)
		{
			Serializer ser;
			ser & v;
			SerializeBuffer sb = ser.buffer();

			m_DB.AddSpendable(blob, NodeDB::Blob(sb.first, (uint32_t) sb.second), 1, 1);
		} else
			m_DB.ModifySpendable(blob, -1, -1);

	return true;
}

void NodeProcessor::DereferenceFossilBlock(uint64_t rowid)
{
	ByteBuffer bbBlock;
	RollbackData rbData;

	m_DB.GetStateBlock(rowid, bbBlock, rbData.m_Buf);

	Block::Body block;

	Deserializer der;
	der.reset(&bbBlock.at(0), bbBlock.size());
	der & block;

	rbData.m_pUtxo = rbData.get_BufAs();

	for (size_t n = 0; n < block.m_vInputs.size(); n++)
	{
		const Input& v = *block.m_vInputs[n];

		UtxoTree::Key::Data d;
		d.m_Commitment = v.m_Commitment;
		d.m_Maturity = rbData.m_pUtxo[n].m_Maturity;


		SpendableKey<UtxoTree::Key, DbType::Utxo> skey;
		skey.m_Key = d;

		m_DB.ModifySpendable(NodeDB::Blob(&skey, sizeof(skey)), -1, 0);
	}

	for (size_t n = 0; n < block.m_vKernelsInput.size(); n++)
	{
		SpendableKey<Merkle::Hash, DbType::Kernel> skey;
		block.m_vKernelsInput[n]->get_HashTotal(skey.m_Key);

		m_DB.ModifySpendable(NodeDB::Blob(&skey, sizeof(skey)), -1, 0);
	}
}

bool NodeProcessor::GoForward(const NodeDB::StateID& sid)
{
	if (HandleBlock(sid, true))
	{
		m_DB.MoveFwd(sid);
		return true;
	}

	m_DB.DelStateBlock(sid.m_Row);
	m_DB.SetStateNotFunctional(sid.m_Row);

	PeerID peer;
	if (m_DB.get_Peer(sid.m_Row, peer))
	{
		m_DB.set_Peer(sid.m_Row, NULL);
		OnPeerInsane(peer);
	}

	return false;
}

void NodeProcessor::Rollback(const NodeDB::StateID& sid)
{
	if (!HandleBlock(sid, false))
		OnCorrupted();

	NodeDB::StateID sid2(sid);
	m_DB.MoveBack(sid2);
}

bool NodeProcessor::get_CurrentState(Block::SystemState::Full& s)
{
	NodeDB::StateID sid;
	if (!m_DB.get_Cursor(sid))
		return false;

	m_DB.get_State(sid.m_Row, s);
	return true;
}

bool NodeProcessor::get_CurrentState(Block::SystemState::ID& id)
{
	Block::SystemState::Full s;
	if (!get_CurrentState(s))
		return false;

	s.get_ID(id);
	return true;
}

bool NodeProcessor::IsRelevantHeight(Height h)
{
	uint64_t hFossil = m_DB.ParamIntGetDef(NodeDB::ParamID::FossilHeight);
	return h >= hFossil + Block::Rules::HeightGenesis;
}

NodeProcessor::DataStatus::Enum NodeProcessor::OnState(const Block::SystemState::Full& s, bool bIgnorePoW, const PeerID& peer)
{
	Block::SystemState::ID id;
	s.get_ID(id);

	if (!s.IsSane())
	{
		LOG_WARNING() << id << " header insane!";
		return DataStatus::Invalid;
	}

	if (!bIgnorePoW && !s.IsValidPoW())
	{
		LOG_WARNING() << id << " PoW invalid";
		return DataStatus::Invalid;
	}

	Timestamp ts = time(NULL);
	if (s.m_TimeStamp > ts)
	{
		ts = s.m_TimeStamp - ts; // dt
		if (ts > Block::Rules::TimestampAheadThreshold_s)
		{
			LOG_WARNING() << id << " Timestamp ahead by " << ts;
			return DataStatus::Invalid;
		}
	}

	if (!IsRelevantHeight(s.m_Height))
		return DataStatus::Rejected;

	if (m_DB.StateFindSafe(id))
		return DataStatus::Rejected;

	NodeDB::Transaction t(m_DB);
	uint64_t rowid = m_DB.InsertState(s);
	m_DB.set_Peer(rowid, &peer);
	t.Commit();

	LOG_INFO() << id << " Header accepted";

	return DataStatus::Accepted;
}

NodeProcessor::DataStatus::Enum NodeProcessor::OnBlock(const Block::SystemState::ID& id, const NodeDB::Blob& block, const PeerID& peer)
{
	if (block.n > Block::Rules::MaxBodySize)
	{
		LOG_WARNING() << id << " Block too large: " << block.n;
		return DataStatus::Invalid;
	}

	uint64_t rowid = m_DB.StateFindSafe(id);
	if (!rowid)
	{
		LOG_WARNING() << id << " Block unexpected";
		return DataStatus::Rejected;
	}

	if (NodeDB::StateFlags::Functional & m_DB.GetStateFlags(rowid))
	{
		LOG_WARNING() << id << " Block already received";
		return DataStatus::Rejected;
	}

	NodeDB::Transaction t(m_DB);

	m_DB.SetStateBlock(rowid, block);
	m_DB.SetStateFunctional(rowid);
	m_DB.set_Peer(rowid, &peer);

	LOG_INFO() << id << " Block accepted";

	if (NodeDB::StateFlags::Reachable & m_DB.GetStateFlags(rowid))
		TryGoUp();

	t.Commit();

	return DataStatus::Accepted;
}

bool NodeProcessor::IsStateNeeded(const Block::SystemState::ID& id)
{
	return IsRelevantHeight(id.m_Height) && !m_DB.StateFindSafe(id);
}

/////////////////////////////
// TxPool
struct SerializerSizeCounter
{
	struct Counter
	{
		size_t m_Value;

		size_t write(const void *ptr, const size_t size)
		{
			m_Value += size;
			return size;
		}

	} m_Counter;

	yas::binary_oarchive<Counter, SERIALIZE_OPTIONS> _oa;


	SerializerSizeCounter() : _oa(m_Counter)
	{
		m_Counter.m_Value = 0;
	}

	template <typename T> SerializerSizeCounter& operator & (const T& object)
	{
		_oa & object;
		return *this;
	}
};

bool NodeProcessor::TxPool::AddTx(Transaction::Ptr&& pValue, Height h)
{
	assert(pValue);

	TxBase::Context ctx;
	if (!pValue->IsValid(ctx))
		return false;

	if ((h < ctx.m_hMin) || (h > ctx.m_hMax))
		return false;

	SerializerSizeCounter ssc;
	ssc & pValue;

	Element* p = new Element;
	p->m_Threshold.m_Value	= ctx.m_hMax;
	p->m_Profit.m_Fee	= ctx.m_Fee.Hi ? Amount(-1) : ctx.m_Fee.Lo; // ignore huge fees (which are  highly unlikely), saturate.
	p->m_Profit.m_nSize	= ssc.m_Counter.m_Value;
	p->m_Tx.m_pValue = std::move(pValue);

	m_setThreshold.insert(p->m_Threshold);
	m_setProfit.insert(p->m_Profit);
	m_setTxs.insert(p->m_Tx);

	return true;
}

void NodeProcessor::TxPool::Delete(Element& x)
{
	m_setThreshold.erase(ThresholdSet::s_iterator_to(x.m_Threshold));
	m_setProfit.erase(ProfitSet::s_iterator_to(x.m_Profit));
	m_setTxs.erase(TxSet::s_iterator_to(x.m_Tx));
	delete &x;
}

void NodeProcessor::TxPool::DeleteOutOfBound(Height h)
{
	while (!m_setThreshold.empty())
	{
		Element::Threshold& t = *m_setThreshold.begin();
		if (t.m_Value >= h)
			break;

		Delete(t.get_ParentObj());
	}
}

void NodeProcessor::TxPool::ShrinkUpTo(uint32_t nCount)
{
	while (m_setProfit.size() > nCount)
		Delete(m_setProfit.rbegin()->get_ParentObj());
}

void NodeProcessor::TxPool::Clear()
{
	while (!m_setThreshold.empty())
		Delete(m_setThreshold.begin()->get_ParentObj());
}

bool NodeProcessor::TxPool::Element::Tx::operator < (const Tx& t) const
{
	assert(m_pValue && t.m_pValue);
	// TODO: Normally we can account for tx offset only, different transactions highly unlikely to have the same offset.
	// But current wallet implementation doesn't use the offset.
	return m_pValue->cmp(*t.m_pValue) < 0;
}

bool NodeProcessor::TxPool::Element::Profit::operator < (const Profit& t) const
{
	// handle overflow. To be precise need to use big-int (128-bit) arithmetics
	//	return m_Fee * t.m_nSize > t.m_Fee * m_nSize;

	typedef ECC::uintBig_t<128> uint128;

	uint128 f0, s0, f1, s1;
	f0 = m_Fee;
	s0 = m_nSize;
	f1 = t.m_Fee;
	s1 = t.m_nSize;

	f0 = f0 * s1;
	f1 = f1 * s0;

	return f0 > f1;
}

/////////////////////////////
// Block generation
template <typename T>
void AppendCloneArray(Serializer& ser, std::vector<T>& trg, const std::vector<T>& src)
{
	size_t i0 = trg.size();
	trg.resize(i0 + src.size());

	for (size_t i = 0; i < src.size(); i++)
	{
		ser.reset();
		ser & src[i];
		SerializeBuffer sb = ser.buffer();

		Deserializer der;
		der.reset(sb.first, sb.second);
		der & trg[i0 + i];
	}
}

Height NodeProcessor::get_NextHeight()
{
	NodeDB::StateID sid;
	m_DB.get_Cursor(sid);

	return sid.m_Height + 1;
}

uint8_t NodeProcessor::get_NextDifficulty()
{
	NodeDB::StateID sid;
	if (!m_DB.get_Cursor(sid))
		return 0; // 1st block difficulty 0

	Block::SystemState::Full s;
	m_DB.get_State(sid.m_Row, s);

	Height dh = s.m_Height - Block::Rules::HeightGenesis;

	if (!dh || (dh % Block::Rules::DifficultyReviewCycle))
		return s.m_PoW.m_Difficulty; // no change

	// review the difficulty
	NodeDB::WalkerState ws(m_DB);
	m_DB.EnumStatesAt(ws, s.m_Height - Block::Rules::DifficultyReviewCycle);
	while (true)
	{
		if (!ws.MoveNext())
			OnCorrupted();

		if (NodeDB::StateFlags::Active & m_DB.GetStateFlags(ws.m_Sid.m_Row))
			break;
	}

	Block::SystemState::Full s2;
	m_DB.get_State(ws.m_Sid.m_Row, s2);

	Block::Rules::AdjustDifficulty(s.m_PoW.m_Difficulty, s2.m_TimeStamp, s.m_TimeStamp);
	return s.m_PoW.m_Difficulty;
}

Timestamp NodeProcessor::get_MovingMedian()
{
	NodeDB::StateID sid;
	if (!m_DB.get_Cursor(sid))
		return 0;

	Timestamp pArr[Block::Rules::WindowForMedian];
	uint32_t n = 0;

	while (true)
	{
		Block::SystemState::Full s;
		m_DB.get_State(sid.m_Row, s);
		pArr[n] = s.m_TimeStamp;

		if (Block::Rules::WindowForMedian == ++n)
			break;

		if (!m_DB.get_Prev(sid))
			break;
	}

	std::sort(pArr, pArr + n); // there's a better algorithm to find a median (or whatever order), however our array isn't too big, so it's ok.

	return pArr[n >> 1];
}

bool NodeProcessor::GenerateNewBlock(TxPool& txp, Block::SystemState::Full& s, Block::Body& res, Amount& fees, Height h, RollbackData& rbData)
{
	fees = 0;

	size_t nBlockSize = 0;

	// due to (potential) inaccuracy in the block size estimation, our rough estimate - take no more than 95% of allowed block size, minus potential UTXOs to consume fees and coinbase.
	const size_t nRoughExtra = sizeof(ECC::Point) * 2 + sizeof(ECC::RangeProof::Confidential) + sizeof(ECC::RangeProof::Public) + 300;
	const size_t nSizeThreshold = Block::Rules::MaxBodySize * 95 / 100 - nRoughExtra;

	ECC::Scalar::Native offset = res.m_Offset;

	Serializer ser;

	for (TxPool::ProfitSet::iterator it = txp.m_setProfit.begin(); txp.m_setProfit.end() != it; )
	{
		TxPool::Element& x = (it++)->get_ParentObj();
		if (nBlockSize + x.m_Profit.m_nSize > nSizeThreshold)
			break;

		Transaction& tx = *x.m_Tx.m_pValue;
		rbData.Prepare(tx);

		if (HandleValidatedTx(tx, h, true, rbData))
		{
			// Clone the transaction before copying it to the block. We're forced to do this because they're not shared.
			// TODO: Fix this!

			AppendCloneArray(ser, res.m_vInputs, tx.m_vInputs);
			AppendCloneArray(ser, res.m_vOutputs, tx.m_vOutputs);
			AppendCloneArray(ser, res.m_vKernelsInput, tx.m_vKernelsInput);
			AppendCloneArray(ser, res.m_vKernelsOutput, tx.m_vKernelsOutput);

			fees += x.m_Profit.m_Fee;
			offset += ECC::Scalar::Native(tx.m_Offset);
			nBlockSize += x.m_Profit.m_nSize;

		}
		else
		{
			rbData.Unprepare(tx);
			txp.Delete(x); // isn't available in this context
		}
	}

	if (fees)
	{
		ECC::Scalar::Native kFee;
		DeriveKey(kFee, m_Kdf, h, KeyType::Comission);

		Output::Ptr pOutp(new Output);
		pOutp->Create(kFee, fees);

		if (!HandleBlockElement(*pOutp, h, true))
			return false; // though should not happen!

		res.m_vOutputs.push_back(std::move(pOutp));

		kFee = -kFee;
		offset += kFee;
	}
	else
	{
		ECC::Scalar::Native kKernel;
		DeriveKey(kKernel, m_Kdf, h, KeyType::Kernel);

		TxKernel::Ptr pKrn(new TxKernel);
		pKrn->m_Excess = ECC::Point::Native(ECC::Context::get().G * kKernel);

		ECC::Hash::Value hv;
		pKrn->get_HashForSigning(hv);
		pKrn->m_Signature.Sign(hv, kKernel);

		if (!HandleBlockElement(*pKrn, true, false))
			return false; // Will fail if kernel key duplicated!

		res.m_vKernelsOutput.push_back(std::move(pKrn));

		kKernel = -kKernel;
		offset += kKernel;
	}

	ECC::Scalar::Native kCoinbase;
	DeriveKey(kCoinbase, m_Kdf, h, KeyType::Coinbase);

	Output::Ptr pOutp(new Output);
	pOutp->m_Coinbase = true;
	pOutp->Create(kCoinbase, Block::Rules::CoinbaseEmission, true);

	if (!HandleBlockElement(*pOutp, h, true))
		return false;

	res.m_vOutputs.push_back(std::move(pOutp));

	kCoinbase = -kCoinbase;
	offset += kCoinbase;
	res.m_Subsidy += Block::Rules::CoinbaseEmission;

	// Finalize block construction.
	if (h > Block::Rules::HeightGenesis)
	{
		NodeDB::StateID sid;
		m_DB.get_Cursor(sid);

		m_DB.get_State(sid.m_Row, s);
		s.get_Hash(s.m_Prev);
		m_DB.get_PredictedStatesHash(s.m_History, sid);
	}
	else
	{
		ZeroObject(s.m_Prev);
		ZeroObject(s.m_History);
	}

	s.m_Height = h;
	get_CurrentLive(s.m_LiveObjects);

	s.m_PoW.m_Difficulty = get_NextDifficulty();
	s.m_TimeStamp = time(NULL); // TODO: 64-bit time

	// Adjust the timestamp to be no less than the moving median (otherwise the block'll be invalid)
	Timestamp tm = get_MovingMedian() + 1;
	s.m_TimeStamp = std::max(s.m_TimeStamp, tm);

	res.m_Offset = offset;

	return true;
}

bool NodeProcessor::GenerateNewBlock(TxPool& txp, Block::SystemState::Full& s, ByteBuffer& bbBlock, Amount& fees)
{
	Block::Body block;
	block.ZeroInit();
	return GenerateNewBlock(txp, s, bbBlock, fees, block, true);
}

bool NodeProcessor::GenerateNewBlock(TxPool& txp, Block::SystemState::Full& s, ByteBuffer& bbBlock, Amount& fees, Block::Body& res)
{
	return GenerateNewBlock(txp, s, bbBlock, fees, res, false);
}

bool NodeProcessor::GenerateNewBlock(TxPool& txp, Block::SystemState::Full& s, ByteBuffer& bbBlock, Amount& fees, Block::Body& res, bool bInitiallyEmpty)
{
	Height h = get_NextHeight();

	if (!bInitiallyEmpty && !VerifyBlock(res, h, h))
		return false;

	{
		NodeDB::Transaction t(m_DB);

		RollbackData rbData;

		if (!bInitiallyEmpty)
		{
			rbData.Prepare(res);
			if (!HandleValidatedTx(res, h, true, rbData))
				return false;
		}

		bool bRes = GenerateNewBlock(txp, s, res, fees, h, rbData);

		if (!HandleValidatedTx(res, h, false, rbData)) // undo changes
			OnCorrupted();

		res.Sort(); // can sort only after the changes are undone.
		res.DeleteIntermediateOutputs();
	}

	Serializer ser;

	ser.reset();
	ser & res;
	ser.swap_buf(bbBlock);

	return bbBlock.size() <= Block::Rules::MaxBodySize;
}

bool NodeProcessor::VerifyBlock(const Block::Body& block, Height h0, Height h1)
{
	return block.IsValid(h0, h1);
}

void NodeProcessor::ExportMacroBlock(Block::Body& res)
{
	struct Walker
		:public UnspentWalker
	{
		Walker(NodeProcessor& me) :UnspentWalker(me, true) {}

		Block::Body* m_pRes;

		virtual bool OnUtxo(const UtxoTree::Key& key) override
		{
			UtxoSig sig;

			Deserializer der;
			der.reset(m_Signature.p, m_Signature.n);
			der & sig;

			UtxoTree::Key::Data d;
			d = key;

			Output::Ptr pOutp(new Output);
			pOutp->m_Commitment	= d.m_Commitment;
			pOutp->m_Coinbase	= sig.m_Coinbase;
			pOutp->m_Incubation	= sig.m_Incubation;
			pOutp->m_pConfidential.swap(sig.m_pConfidential);
			pOutp->m_pPublic.swap(sig.m_pPublic);

			// calculate hDelta, to fit the needed maturity
			pOutp->m_hDelta = d.m_Maturity - sig.m_Incubation - Block::Rules::HeightGenesis;
			pOutp->m_hDelta -= sig.m_Coinbase ? Block::Rules::MaturityCoinbase : Block::Rules::MaturityStd;

			m_pRes->m_vOutputs.push_back(std::move(pOutp));

			return true;
		}

		virtual bool OnKernel(const Merkle::Hash&) override
		{
			TxKernel::Ptr pKrn(new TxKernel);

			Deserializer der;
			der.reset(m_Signature.p, m_Signature.n);
			der & *pKrn;

			m_pRes->m_vKernelsOutput.push_back(std::move(pKrn));

			return true;
		}
	};

	Walker wlk(*this);
	wlk.m_pRes = &res;
	wlk.Traverse();

	res.Sort();

	NodeDB::Blob blob(res.m_Offset.m_Value.m_pData, sizeof(res.m_Offset.m_Value.m_pData));

	if (!m_DB.ParamGet(NodeDB::ParamID::StateExtra, NULL, &blob))
		res.m_Offset.m_Value = ECC::Zero;

	res.m_Subsidy.Lo = m_DB.ParamIntGetDef(NodeDB::ParamID::SubsidyLo);
	res.m_Subsidy.Hi = m_DB.ParamIntGetDef(NodeDB::ParamID::SubsidyHi);
}

} // namespace beam
