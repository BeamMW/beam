#include "node_processor.h"
#include "../core/block_crypt.h"
#include "../utility/serialize.h"
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
			OnCorrupted();

		uint8_t nType = *(uint8_t*) m_Key.p;
		((uint8_t*&) m_Key.p)++;
		m_Key.n--;

		switch (nType)
		{
		case DbType::Utxo:
		{
			if (UtxoTree::Key::s_Bytes != m_Key.n)
				OnCorrupted();

			static_assert(sizeof(UtxoTree::Key) == UtxoTree::Key::s_Bytes, "");

			if (!OnUtxo(*(UtxoTree::Key*) m_Key.p))
				return false;
		}
		break;

		case DbType::Kernel:
		{
			if (sizeof(Merkle::Hash) != m_Key.n)
				OnCorrupted();

			if (!OnKernel(*(Merkle::Hash*) m_Key.p))
				return false;

		}
		break;

		default:
			OnCorrupted();
		}
	}
}

void NodeProcessor::Initialize(const char* szPath)
{
	m_DB.Open(szPath);

	InitCursor();

	if (!m_Cursor.m_SubsidyOpen)
		OnSubsidyOptionChanged(m_Cursor.m_SubsidyOpen);

	// Load all the 'live' data
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

void NodeProcessor::InitCursor()
{
	if (m_DB.get_Cursor(m_Cursor.m_Sid))
	{
		m_DB.get_State(m_Cursor.m_Sid.m_Row, m_Cursor.m_Full);
		m_Cursor.m_Full.get_ID(m_Cursor.m_ID);

		m_DB.get_PredictedStatesHash(m_Cursor.m_HistoryNext, m_Cursor.m_Sid);

		NodeDB::StateID sid = m_Cursor.m_Sid;
		if (m_DB.get_Prev(sid))
			m_DB.get_PredictedStatesHash(m_Cursor.m_History, sid);
		else
			ZeroObject(m_Cursor.m_History);

		m_Cursor.m_DifficultyNext = get_NextDifficulty();
	}
	else
		ZeroObject(m_Cursor);

	m_Cursor.m_SubsidyOpen = 0 != (m_DB.ParamIntGetDef(NodeDB::ParamID::SubsidyOpen, 1));
}

void NodeProcessor::EnumCongestions()
{
	// request all potentially missing data
	NodeDB::WalkerState ws(m_DB);
	for (m_DB.EnumTips(ws); ws.MoveNext(); )
	{
		NodeDB::StateID& sid = ws.m_Sid; // alias
		if (NodeDB::StateFlags::Reachable & m_DB.GetStateFlags(sid.m_Row))
			continue;

		if (sid.m_Height < m_Cursor.m_Sid.m_Height)
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
		NodeDB::StateID sidTrg;

		{
			NodeDB::WalkerState ws(m_DB);
			m_DB.EnumFunctionalTips(ws);

			if (!ws.MoveNext())
			{
				assert(!m_Cursor.m_Sid.m_Row);
				break; // nowhere to go
			}
			sidTrg = ws.m_Sid;
		}

		assert(sidTrg.m_Height >= m_Cursor.m_Sid.m_Height);
		if (sidTrg.m_Height == m_Cursor.m_Sid.m_Height)
			break; // already at maximum height (though maybe at different tip)

		// Calculate the path
		std::vector<uint64_t> vPath;
		while (sidTrg.m_Row != m_Cursor.m_Sid.m_Row)
		{
			assert(sidTrg.m_Row);
			vPath.push_back(sidTrg.m_Row);

			if (m_Cursor.m_Sid.m_Height == sidTrg.m_Height)
			{
				OnRolledBack();
				Rollback();
				bDirty = true;
			}

			if (!m_DB.get_Prev(sidTrg))
				sidTrg.SetNull();
		}

		bool bPathOk = true;

		for (size_t i = vPath.size(); i--; )
		{
			bDirty = true;
			if (!GoForward(vPath[i]))
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
		PruneOld();
		OnNewState();
	}
}

void NodeProcessor::PruneOld()
{
	Height h = m_Cursor.m_Sid.m_Height;
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

void NodeProcessor::get_Definition(Merkle::Hash& hv, const Merkle::Hash& hvHist)
{
	get_CurrentLive(hv);
	Merkle::Interpret(hv, hvHist, false);
}

struct NodeProcessor::RollbackData
{
	// helper structures for rollback
	struct Utxo {
		Height m_Maturity; // the extra info we need to restore an UTXO, in addition to the Input.
	};

	ByteBuffer m_Buf;
	size_t m_Inputs;

	RollbackData() :m_Inputs(0) {}

	Utxo& NextInput(bool bWrite)
	{
		size_t nSize = (m_Inputs + 1) * sizeof(Utxo);
		if (nSize > m_Buf.size())
		{
			if (!bWrite)
				OnCorrupted();
			m_Buf.resize(nSize);
		}

		return ((Utxo*) &m_Buf.at(0))[m_Inputs++];
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
		if (rbData.m_Buf.empty())
		{
			bFirstTime = true;

			if (!m_Cursor.m_SubsidyOpen && block.m_SubsidyClosing)
			{
				LOG_WARNING() << id << " illegal subsidy-close flag";
				return false;
			}

			if (m_Cursor.m_DifficultyNext != s.m_PoW.m_Difficulty)
			{
				LOG_WARNING() << id << " Difficulty expected=" << uint32_t(m_Cursor.m_DifficultyNext) << ", actual=" << uint32_t(s.m_PoW.m_Difficulty);
				return false;
			}

			if (s.m_TimeStamp <= get_MovingMedian())
			{
				LOG_WARNING() << id << " Timestamp inconsistent wrt median";
				return false;
			}

			if (!VerifyBlock(block, block.get_Reader(), sid.m_Height))
			{
				LOG_WARNING() << id << " context-free verification failed";
				return false;
			}
		}
	} else
		assert(!rbData.m_Buf.empty());


	bool bOk = HandleValidatedTx(block.get_Reader(), sid.m_Height, bFwd, rbData);
	if (!bOk)
		LOG_WARNING() << id << " invalid in its context";

	if (block.m_SubsidyClosing)
		OnSubsidyOptionChanged(!bFwd);

	if (bFirstTime && bOk)
	{
		// check the validity of state description.
		Merkle::Hash hvDef;
		get_Definition(hvDef, m_Cursor.m_HistoryNext);

		if (s.m_Definition != hvDef)
		{
			LOG_WARNING() << id << " Header Definition mismatch";
			bOk = false;
		}

		if (bOk)
		{
			if (rbData.m_Inputs)
				m_DB.SetStateRollback(sid.m_Row, NodeDB::Blob(&rbData.m_Buf.at(0), sizeof(RollbackData::Utxo) * rbData.m_Inputs));
			else
			{
				// make sure it's not empty, even if there were no inputs, this is how we distinguish processed blocks.
				uint8_t zero = 0;
				m_DB.SetStateRollback(sid.m_Row, NodeDB::Blob(&zero, 1));

			}
		}
		else
		{
			if (block.m_SubsidyClosing)
			{
				assert(bFwd);
				OnSubsidyOptionChanged(bFwd);
			}

			rbData.m_Inputs = 0;
			verify(HandleValidatedTx(block.get_Reader(), sid.m_Height, false, rbData));
		}
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

		if (block.m_SubsidyClosing)
		{
			uint64_t nVal = !bFwd;
			m_DB.ParamSet(NodeDB::ParamID::SubsidyOpen, &nVal, NULL);
		}

		LOG_INFO() << id << " Block interpreted. Fwd=" << bFwd;
	}

	return bOk;
}

bool NodeProcessor::HandleValidatedTx(TxBase::IReader&& r, Height h, bool bFwd, RollbackData& rbData, bool bCompressed)
{
	size_t nInp = 0, nOut = 0, nKrnInp = 0, nKrnOut = 0;
	r.Reset();

	bool bOk = true;
	for (; r.m_pUtxoIn; r.NextUtxoIn(), nInp++)
		if (!HandleBlockElement(*r.m_pUtxoIn, h, bFwd, rbData))
		{
			bOk = false;
			break;
		}

	if (bOk)
		for (; r.m_pUtxoOut; r.NextUtxoOut(), nOut++)
			if (!HandleBlockElement(*r.m_pUtxoOut, h, bFwd, bCompressed))
			{
				bOk = false;
				break;
			}

	if (bOk)
		for (; r.m_pKernelIn; r.NextKernelIn(), nKrnInp++)
			if (!HandleBlockElement(*r.m_pKernelIn, bFwd, true))
			{
				bOk = false;
				break;
			}

	if (bOk)
		for (; r.m_pKernelOut; r.NextKernelOut(), nKrnOut++)
			if (!HandleBlockElement(*r.m_pKernelOut, bFwd, false))
			{
				bOk = false;
				break;
			}

	if (bOk)
		return true;

	if (!bFwd)
		OnCorrupted();

	// Rollback all the changes. Must succeed!
	r.Reset();

	for (; nKrnOut--; r.NextKernelOut())
		HandleBlockElement(*r.m_pKernelOut, false, false);

	for (; nKrnInp--; r.NextKernelIn())
		HandleBlockElement(*r.m_pKernelIn, false, true);

	for (; nOut--; r.NextUtxoOut())
		HandleBlockElement(*r.m_pUtxoOut, h, false, bCompressed);

	rbData.m_Inputs -= nInp;
	size_t n = rbData.m_Inputs;

	for (; nInp--; r.NextUtxoIn())
		HandleBlockElement(*r.m_pUtxoIn, h, false, rbData);

	rbData.m_Inputs = n;

	return false;
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

bool NodeProcessor::HandleBlockElement(const Input& v, Height h, bool bFwd, RollbackData& rbData)
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

		if (v.m_Maturity > h)
			return false;

		d.m_Maturity = v.m_Maturity;
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

		rbData.NextInput(true).m_Maturity = d.m_Maturity;

	} else
	{
		d.m_Maturity = rbData.NextInput(false).m_Maturity;
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

void HeightAdd(Height& trg, Height val)
{
	trg += val;
	if (trg < val)
		trg = Height(-1);
}

bool NodeProcessor::HandleBlockElement(const Output& v, Height h, bool bFwd, bool bCompressed)
{
	UtxoTree::Key::Data d;
	d.m_Commitment = v.m_Commitment;
	d.m_Maturity = h;
	HeightAdd(d.m_Maturity, v.m_Coinbase ? Block::Rules::MaturityCoinbase : Block::Rules::MaturityStd);
	HeightAdd(d.m_Maturity, v.m_Incubation);

	if (v.m_Maturity >= Block::Rules::HeightGenesis)
	{
		if (!bCompressed)
			return false; // maturity forgery isn't allowed
		if (v.m_Maturity < d.m_Maturity)
			return false; // decrease not allowed

		d.m_Maturity = v.m_Maturity;
	}

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

void NodeProcessor::OnSubsidyOptionChanged(bool bOpen)
{
	bool bAdd = !bOpen;

	Merkle::Hash hv;
	hv = ECC::Zero;

	RadixHashOnlyTree::Cursor cu;
	bool bCreate = true;
	m_Kernels.Find(cu, hv, bCreate);

	assert(bAdd == bCreate);
	if (!bAdd)
		m_Kernels.Delete(cu);
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

	Block::Body::Reader r = block.get_Reader();
	r.Reset();

	for (; r.m_pUtxoIn; r.NextUtxoIn())
	{
		UtxoTree::Key::Data d;
		d.m_Commitment = r.m_pUtxoIn->m_Commitment;
		d.m_Maturity = rbData.NextInput(false).m_Maturity;

		SpendableKey<UtxoTree::Key, DbType::Utxo> skey;
		skey.m_Key = d;

		m_DB.ModifySpendable(NodeDB::Blob(&skey, sizeof(skey)), -1, 0);
	}

	for (; r.m_pKernelIn; r.NextKernelIn())
	{
		SpendableKey<Merkle::Hash, DbType::Kernel> skey;
		r.m_pKernelIn->get_HashTotal(skey.m_Key);

		m_DB.ModifySpendable(NodeDB::Blob(&skey, sizeof(skey)), -1, 0);
	}
}

bool NodeProcessor::GoForward(uint64_t row)
{
	NodeDB::StateID sid;
	sid.m_Height = m_Cursor.m_Sid.m_Height + 1;
	sid.m_Row = row;

	if (HandleBlock(sid, true))
	{
		m_DB.MoveFwd(sid);
		InitCursor();
		return true;
	}

	m_DB.DelStateBlock(row);
	m_DB.SetStateNotFunctional(row);

	PeerID peer;
	if (m_DB.get_Peer(row, peer))
	{
		m_DB.set_Peer(row, NULL);
		OnPeerInsane(peer);
	}

	return false;
}

void NodeProcessor::Rollback()
{
	NodeDB::StateID sid = m_Cursor.m_Sid;
	m_DB.MoveBack(m_Cursor.m_Sid);
	InitCursor();

	if (!HandleBlock(sid, false))
		OnCorrupted();
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
bool NodeProcessor::ValidateTx(const Transaction& tx, Transaction::Context& ctx)
{
	if (!tx.IsValid(ctx))
		return false;

	return ctx.m_Height.IsInRange(m_Cursor.m_Sid.m_Height + 1);
}

void NodeProcessor::TxPool::AddValidTx(Transaction::Ptr&& pValue, const Transaction::Context& ctx, const Transaction::KeyType& key)
{
	assert(pValue);

	SerializerSizeCounter ssc;
	ssc & pValue;

	Element* p = new Element;
	p->m_pValue = std::move(pValue);
	p->m_Threshold.m_Value	= ctx.m_Height.m_Max;
	p->m_Profit.m_Fee	= ctx.m_Fee.Hi ? Amount(-1) : ctx.m_Fee.Lo; // ignore huge fees (which are  highly unlikely), saturate.
	p->m_Profit.m_nSize	= ssc.m_Counter.m_Value;
	p->m_Tx.m_Key = key;

	m_setThreshold.insert(p->m_Threshold);
	m_setProfit.insert(p->m_Profit);
	m_setTxs.insert(p->m_Tx);
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
uint8_t NodeProcessor::get_NextDifficulty()
{
	if (!m_Cursor.m_Sid.m_Row)
		return 0; // 1st block difficulty 0

	Height dh = m_Cursor.m_Full.m_Height - Block::Rules::HeightGenesis;

	if (!dh || (dh % Block::Rules::DifficultyReviewCycle))
		return m_Cursor.m_Full.m_PoW.m_Difficulty; // no change

	// review the difficulty
	NodeDB::WalkerState ws(m_DB);
	m_DB.EnumStatesAt(ws, m_Cursor.m_Full.m_Height - Block::Rules::DifficultyReviewCycle);
	while (true)
	{
		if (!ws.MoveNext())
			OnCorrupted();

		if (NodeDB::StateFlags::Active & m_DB.GetStateFlags(ws.m_Sid.m_Row))
			break;
	}

	Block::SystemState::Full s2;
	m_DB.get_State(ws.m_Sid.m_Row, s2);

	uint8_t ret = m_Cursor.m_Full.m_PoW.m_Difficulty;
	Block::Rules::AdjustDifficulty(ret, s2.m_TimeStamp, m_Cursor.m_Full.m_TimeStamp);
	return ret;
}

Timestamp NodeProcessor::get_MovingMedian()
{
	if (!m_Cursor.m_Sid.m_Row)
		return 0;

	Timestamp pArr[Block::Rules::WindowForMedian];
	uint32_t n = 0;

	for (uint64_t row = m_Cursor.m_Sid.m_Row; ; )
	{
		Block::SystemState::Full s;
		m_DB.get_State(row, s);
		pArr[n] = s.m_TimeStamp;

		if (Block::Rules::WindowForMedian == ++n)
			break;

		if (!m_DB.get_Prev(row))
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

	for (TxPool::ProfitSet::iterator it = txp.m_setProfit.begin(); txp.m_setProfit.end() != it; )
	{
		TxPool::Element& x = (it++)->get_ParentObj();
		if (nBlockSize + x.m_Profit.m_nSize > nSizeThreshold)
			break;

		Transaction& tx = *x.m_pValue;

		if (HandleValidatedTx(tx.get_Reader(), h, true, rbData))
		{
			Block::Body::Writer(res).Dump(tx.get_Reader());

			fees += x.m_Profit.m_Fee;
			offset += ECC::Scalar::Native(tx.m_Offset);
			nBlockSize += x.m_Profit.m_nSize;

		}
		else
			txp.Delete(x); // isn't available in this context
	}

	if (fees)
	{
		ECC::Scalar::Native kFee;
		DeriveKey(kFee, m_Kdf, h, KeyType::Comission);

		Output::Ptr pOutp(new Output);
		pOutp->Create(kFee, fees);

		if (!HandleBlockElement(*pOutp, h, true, false))
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

	if (!HandleBlockElement(*pOutp, h, true, false))
		return false;

	res.m_vOutputs.push_back(std::move(pOutp));

	kCoinbase = -kCoinbase;
	offset += kCoinbase;
	res.m_Subsidy += Block::Rules::CoinbaseEmission;

	// Finalize block construction.
	if (m_Cursor.m_Sid.m_Row)
		s.m_Prev = m_Cursor.m_ID.m_Hash;
	else
		ZeroObject(s.m_Prev);

	if (!m_Cursor.m_SubsidyOpen)
		res.m_SubsidyClosing = false;

	if (res.m_SubsidyClosing)
		OnSubsidyOptionChanged(false);

	get_Definition(s.m_Definition, m_Cursor.m_HistoryNext);

	if (res.m_SubsidyClosing)
		OnSubsidyOptionChanged(true);

	s.m_Height = h;
	s.m_PoW.m_Difficulty = m_Cursor.m_DifficultyNext;
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
	block.m_SubsidyClosing = true; // by default insist on it. If already closed - this flag will automatically be turned OFF
	return GenerateNewBlock(txp, s, bbBlock, fees, block, true);
}

bool NodeProcessor::GenerateNewBlock(TxPool& txp, Block::SystemState::Full& s, ByteBuffer& bbBlock, Amount& fees, Block::Body& res)
{
	return GenerateNewBlock(txp, s, bbBlock, fees, res, false);
}

bool NodeProcessor::GenerateNewBlock(TxPool& txp, Block::SystemState::Full& s, ByteBuffer& bbBlock, Amount& fees, Block::Body& res, bool bInitiallyEmpty)
{
	Height h = m_Cursor.m_Sid.m_Height + 1;

	if (!bInitiallyEmpty && !VerifyBlock(res, res.get_Reader(), h))
		return false;

	{
		NodeDB::Transaction t(m_DB);

		RollbackData rbData;

		if (!bInitiallyEmpty)
		{
			if (!HandleValidatedTx(res.get_Reader(), h, true, rbData))
				return false;
		}

		bool bRes = GenerateNewBlock(txp, s, res, fees, h, rbData);

		rbData.m_Inputs = 0;
		verify(HandleValidatedTx(res.get_Reader(), h, false, rbData)); // undo changes

		if (!bRes)
			return false;

		res.Sort(); // can sort only after the changes are undone.
		res.DeleteIntermediateOutputs();
	}

	Serializer ser;

	ser.reset();
	ser & res;
	ser.swap_buf(bbBlock);

	return bbBlock.size() <= Block::Rules::MaxBodySize;
}

bool NodeProcessor::VerifyBlock(const Block::BodyBase& block, TxBase::IReader&& r, const HeightRange& hr)
{
	return block.IsValid(hr, m_Cursor.m_SubsidyOpen, std::move(r));
}

void NodeProcessor::ExtractBlockWithExtra(Block::Body& block, Block::SystemState::Full& s, const NodeDB::StateID& sid)
{
	ByteBuffer bb;
	RollbackData rbData;
	m_DB.GetStateBlock(sid.m_Row, bb, rbData.m_Buf);
	m_DB.get_State(sid.m_Row, s);

	Deserializer der;
	der.reset(bb.empty() ? NULL : &bb.at(0), bb.size());
	der & block;

	for (auto i = 0; i < block.m_vInputs.size(); i++)
		block.m_vInputs[i]->m_Maturity = rbData.NextInput(false).m_Maturity;
}

void NodeProcessor::ExportMacroBlock(Block::BodyBase& res, TxBase::IWriter& w)
{
	struct Walker
		:public UnspentWalker
	{
		Walker(NodeProcessor& me) :UnspentWalker(me, true) {}

		Block::Body* m_pRes;

		virtual bool OnUtxo(const UtxoTree::Key& key) override
		{
			for (Input::Count i = 0; i < m_nUnspentCount; i++)
			{
				UtxoSig sig;

				Deserializer der;
				der.reset(m_Signature.p, m_Signature.n);
				der & sig;

				UtxoTree::Key::Data d;
				d = key;

				Output::Ptr pOutp(new Output);
				pOutp->m_Commitment	= d.m_Commitment;
				pOutp->m_Maturity	= d.m_Maturity;
				pOutp->m_Coinbase	= sig.m_Coinbase;
				pOutp->m_Incubation	= sig.m_Incubation;
				pOutp->m_pConfidential.swap(sig.m_pConfidential);
				pOutp->m_pPublic.swap(sig.m_pPublic);

				m_pRes->m_vOutputs.push_back(std::move(pOutp));
			}
			return true;
		}

		virtual bool OnKernel(const Merkle::Hash&) override
		{
			assert(1 == m_nUnspentCount);

			TxKernel::Ptr pKrn(new TxKernel);

			Deserializer der;
			der.reset(m_Signature.p, m_Signature.n);
			der & *pKrn;

			m_pRes->m_vKernelsOutput.push_back(std::move(pKrn));

			return true;
		}
	};

	// Currently we convert it to a block in memory, because we need to sort the data.
	Block::Body block;

	Walker wlk(*this);
	wlk.m_pRes = &block;
	wlk.Traverse();

	block.Sort();

	w.Dump(block.get_Reader());

	NodeDB::Blob blob(res.m_Offset.m_Value.m_pData, sizeof(res.m_Offset.m_Value.m_pData));

	if (!m_DB.ParamGet(NodeDB::ParamID::StateExtra, NULL, &blob))
		res.m_Offset.m_Value = ECC::Zero;

	res.m_Subsidy.Lo = m_DB.ParamIntGetDef(NodeDB::ParamID::SubsidyLo);
	res.m_Subsidy.Hi = m_DB.ParamIntGetDef(NodeDB::ParamID::SubsidyHi);

	res.m_SubsidyClosing = !m_Cursor.m_SubsidyOpen;
}

bool NodeProcessor::ImportMacroBlock(const Block::SystemState::ID& id, const Block::BodyBase& block, TxBase::IReader&& r)
{
	if (m_Cursor.m_Sid.m_Row)
		return false; // must be at "clean" state

	uint64_t rowid = m_DB.StateFindSafe(id);
	if (!rowid)
		return false;

	Block::SystemState::Full s;
	m_DB.get_State(rowid, s);

	// we must have all the headers
	std::vector<uint64_t> rowids(id.m_Height - Block::Rules::HeightGenesis + 1);
	for (Height h = id.m_Height; ; h--)
	{
		rowids[h - Block::Rules::HeightGenesis] = rowid;

		if (Block::Rules::HeightGenesis == h)
			break;

		if (!m_DB.get_Prev(rowid))
			return false;
	}

	if (!VerifyBlock(block, std::move(r), HeightRange(Block::Rules::HeightGenesis, id.m_Height)))
		return false;

	NodeDB::Transaction t(m_DB);

	RollbackData rbData;
	if (!HandleValidatedTx(std::move(r), Block::Rules::HeightGenesis, true, rbData, true))
		return false;

	// Update DB state flags and cursor. This will also buils the MMR for prev states
	NodeDB::StateID sid;
	for (sid.m_Height = Block::Rules::HeightGenesis; sid.m_Height <= id.m_Height; sid.m_Height++)
	{
		sid.m_Row = rowids[sid.m_Height - Block::Rules::HeightGenesis];
		m_DB.SetStateFunctional(sid.m_Row);

		m_DB.DelStateBlock(sid.m_Row); // if somehow it was downloaded
		m_DB.set_Peer(sid.m_Row, NULL);

		m_DB.MoveFwd(sid);
	}

	uint64_t val = !block.m_SubsidyClosing;
	m_DB.ParamSet(NodeDB::ParamID::SubsidyOpen, &val, NULL);

	InitCursor();

	if (!m_Cursor.m_SubsidyOpen)
		OnSubsidyOptionChanged(m_Cursor.m_SubsidyOpen);

	Merkle::Hash hvDef;
	get_Definition(hvDef, m_Cursor.m_History);

	if (s.m_Definition != hvDef)
	{
		rbData.m_Inputs = 0;
		verify(HandleValidatedTx(std::move(r), Block::Rules::HeightGenesis, false, rbData));

		// DB changes are not reverted explicitly, but they will be reverted by DB transaction rollback.

		ZeroObject(m_Cursor);

		return false;
	}

	// everything's fine
	m_DB.ParamSet(NodeDB::ParamID::FossilHeight, &id.m_Height, NULL);
	t.Commit();

	TryGoUp();

	return true;
}

} // namespace beam
