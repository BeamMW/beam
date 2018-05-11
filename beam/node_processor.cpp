#include "node_processor.h"
#include "../utility/serialize.h"
#include "../core/ecc_native.h"
#include "../core/serialization_adapters.h"

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

void NodeProcessor::Initialize(const char* szPath)
{
	m_DB.Open(szPath);

	// Load all th 'live' data
	{
		NodeDB::WalkerSpendable wsp(m_DB);
		for (m_DB.EnumUnpsent(wsp); wsp.MoveNext(); )
		{
			assert(wsp.m_nUnspentCount);
			if (!wsp.m_Key.n)
				OnCorrupted();

			uint8_t nType = *(uint8_t*) wsp.m_Key.p;
			((uint8_t*&) wsp.m_Key.p)++;
			wsp.m_Key.n--;

			switch (nType)
			{
			case DbType::Utxo:
				{
					if (UtxoTree::Key::s_Bytes != wsp.m_Key.n)
						OnCorrupted();

					static_assert(sizeof(UtxoTree::Key) == UtxoTree::Key::s_Bytes, "");
					const UtxoTree::Key& key = *(UtxoTree::Key*) wsp.m_Key.p;

					UtxoTree::Cursor cu;
					bool bCreate = true;

					m_Utxos.Find(cu, key, bCreate)->m_Value.m_Count = wsp.m_nUnspentCount;
					assert(bCreate);
				}
				break;

			case DbType::Kernel:
				{
					if (sizeof(Merkle::Hash) != wsp.m_Key.n)
						OnCorrupted();

					const Merkle::Hash& key = *(Merkle::Hash*) wsp.m_Key.p;

					RadixHashOnlyTree::Cursor cu;
					bool bCreate = true;

					m_Kernels.Find(cu, key, bCreate);
					assert(bCreate);
				}
				break;

			default:
				OnCorrupted();
			}
		}
	}

	NodeDB::Transaction t(m_DB);
	TryGoUp();
	t.Commit();

	FindCongestionPoints();
}

void NodeProcessor::FindCongestionPoints()
{
	// request all potentially missing data
	NodeDB::WalkerState ws(m_DB);
	for (m_DB.EnumTips(ws); ws.MoveNext(); )
	{
		NodeDB::StateID& sid = ws.m_Sid; // alias
		if (NodeDB::StateFlags::Reachable & m_DB.GetStateFlags(sid.m_Row))
			continue;

		bool bBlock = true;

		while (sid.m_Height)
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

		RequestDataInternal(sid.m_Row, bBlock);
	}
}

void NodeProcessor::RequestDataInternal(uint64_t rowid, bool bBlock)
{
	Block::SystemState::Full s;
	m_DB.get_State(rowid, s);

	Block::SystemState::ID id;

	if (bBlock)
		s.get_ID(id);
	else
	{
		id.m_Height = s.m_Height - 1;
		id.m_Hash = s.m_Prev;
	}

	PeerID peer;
	bool bPeer = m_DB.get_Peer(rowid, peer);

	RequestData(id, bBlock, bPeer ? &peer : NULL);

}

void NodeProcessor::FindCongestionPointsAbove(NodeDB::StateID sid)
{
	while (true)
	{
		if (!(NodeDB::StateFlags::Reachable & m_DB.GetStateFlags(sid.m_Row)))
		{
			RequestDataInternal(sid.m_Row, true);
			break;
		}

		std::vector<uint64_t> vRec;
		uint64_t rowid;

		{
			NodeDB::WalkerState ws(m_DB);
			m_DB.EnumAncestors(ws, sid);
			if (!ws.MoveNext())
				break;

			rowid = ws.m_Sid.m_Row;

			while (ws.MoveNext())
				vRec.push_back(ws.m_Sid.m_Row);
		}

		sid.m_Height++;

		for (size_t i = 0; i < vRec.size(); i++)
		{
			sid.m_Row = vRec[i];
			FindCongestionPointsAbove(sid);
		}

		sid.m_Row = rowid;
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

		// Calculate the path
		std::vector<uint64_t> vPath;
		while (sidTrg.m_Row != sidPos.m_Row)
		{
			assert(sidTrg.m_Row);
			vPath.push_back(sidTrg.m_Row);

			if (sidPos.m_Row && (sidPos.m_Height == sidTrg.m_Height))
			{
				Rollback(sidPos);
				bDirty = true;

				if (!m_DB.get_Prev(sidPos))
					ZeroObject(sidPos);
			}

			if (!m_DB.get_Prev(sidTrg))
				ZeroObject(sidTrg);
		}

		bool bPathOk = true;

		for (size_t i = vPath.size(); i--; )
		{
			if (sidPos.m_Row)
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
			m_DB.EnumStatesAt(ws, hFossil);
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

	size_t get_Utxos() const { return m_pUtxo - get_BufAs(); }
};

bool NodeProcessor::HandleBlock(const NodeDB::StateID& sid, bool bFwd)
{
	ByteBuffer bb;
	RollbackData rbData;
	m_DB.GetStateBlock(sid.m_Row, bb, rbData.m_Buf);

	Block::Body block;
	try {

		Deserializer der;
		der.reset(bb.empty() ? NULL : &bb.at(0), bb.size());
		der & block;
	}
	catch (const std::exception&) {
		return false;
	}

	bb.clear();

	bool bFirstTime = false;
	Block::SystemState::Full s;

	if (bFwd)
	{
		size_t n = std::max(size_t(1), block.m_vInputs.size() * sizeof(RollbackData::Utxo));
		if (rbData.m_Buf.size() != n)
		{
			bFirstTime = true;

			m_DB.get_State(sid.m_Row, s);

			Merkle::Proof proof;
			m_DB.get_Proof(proof, sid, sid.m_Height);

			Merkle::Interpret(s.m_Prev, proof);
			if (s.m_History != s.m_Prev)
				return false; // The state (even the header) is formed incorrectly!

			if (!block.IsValid(sid.m_Height, sid.m_Height))
				return false;

			rbData.m_Buf.resize(n);
		}
	} else
		assert(!rbData.m_Buf.empty());


	rbData.m_pUtxo = rbData.get_BufAs();
	if (!bFwd)
		rbData.m_pUtxo += block.m_vInputs.size();

	bool bOk = HandleValidatedTx(block, sid.m_Height, bFwd, rbData);

	if (bFirstTime && bOk)
	{
		// check the validity of state description.
		Merkle::Hash hv;
		get_CurrentLive(hv);

		if (s.m_LiveObjects != hv)
			bOk = false;

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
	UtxoTree::Key::Data d;
	d.m_Commitment = v.m_Commitment;

	if (bFwd)
		d.m_Maturity = 0; // find min
	else
	{
		const RollbackData::Utxo& x = *--rbData.m_pUtxo;
		d.m_Maturity = x.m_Maturity;
	}

	SpendableKey<UtxoTree::Key, DbType::Utxo> skey;
	skey.m_Key = d;

	UtxoTree::Cursor cu;
	bool bCreate = !bFwd;
	UtxoTree::MyLeaf* p = m_Utxos.Find(cu, skey.m_Key, bCreate);

	if (!p && bFwd)
	{
		// try to find the closest match
		struct Traveler :public UtxoTree::ITraveler
		{
			UtxoTree::MyLeaf* m_pLeaf;
			virtual bool OnLeaf(const RadixTree::Leaf& x) override
			{
				m_pLeaf = (UtxoTree::MyLeaf*) &x;
				return false; // stop iteration
			}
		};

		Traveler t;
		if (UtxoTree::Traverse(cu, t))
			return false; // not found

		d = t.m_pLeaf->m_Key;

		if (v.m_Commitment != d.m_Commitment)
			return false; // not found

		if (d.m_Maturity > h)
			return false; // not mature enough!

		p = t.m_pLeaf;
		skey.m_Key = t.m_pLeaf->m_Key;

	}

	cu.Invalidate();

	if (bFwd)
	{
		assert(p->m_Value.m_Count); // we don't store zeroes

		if (! --p->m_Value.m_Count)
			m_Utxos.Delete(cu);

		rbData.m_pUtxo->m_Maturity = d.m_Maturity;
		rbData.m_pUtxo++;

	} else
	{
		if (bCreate)
			p->m_Value.m_Count = 1;
		else
			p->m_Value.m_Count++;
	}

	m_DB.ModifySpendable(NodeDB::Blob(&skey, sizeof(skey)), 0, bFwd ? -1 : 1);
	return true;
}

bool NodeProcessor::HandleBlockElement(const Output& v, Height h, bool bFwd)
{
	UtxoTree::Key::Data d;
	d.m_Commitment = v.m_Commitment;
	d.m_Maturity = h;
	d.m_Maturity += v.m_Coinbase ? Block::s_MaturityCoinbase : Block::s_MaturityStd;

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
			if (v.m_pConfidential)
				ser & *v.m_pConfidential;
			else
				ser & *v.m_pPublic;

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
	get_KrnKey(skey.m_Key, v);

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
		get_KrnKey(skey.m_Key, *block.m_vKernelsInput[n]);

		m_DB.ModifySpendable(NodeDB::Blob(&skey, sizeof(skey)), -1, 0);
	}
}

void NodeProcessor::get_KrnKey(Merkle::Hash& hv, const TxKernel& v)
{
	v.get_Hash(hv);
	ECC::Hash::Processor() << hv << v.m_Excess << v.m_Multiplier >> hv; // add the public excess, it's not included by default
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

	RequestDataInternal(sid.m_Row, true);

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
	return !hFossil || (h > hFossil);
}

bool NodeProcessor::OnState(const Block::SystemState::Full& s, const NodeDB::Blob& /*pow*/, const PeerID& peer)
{
	if (!IsRelevantHeight(s.m_Height))
		return false;

	Block::SystemState::ID id;
	s.get_ID(id);
	if (m_DB.StateFindSafe(id))
		return true;

	NodeDB::Transaction t(m_DB);
	uint64_t rowid = m_DB.InsertState(s);
	m_DB.set_Peer(rowid, &peer);
	t.Commit();

	if (s.m_Height)
	{
		uint64_t rowPrev = rowid;
		if (!m_DB.get_Prev(rowPrev))
		{
			RequestDataInternal(rowid, false);
			return true;

		}

		if (!(m_DB.GetStateFlags(rowPrev) & NodeDB::StateFlags::Reachable))
			return true;
	}

	RequestDataInternal(rowid, true);

	return true;
}

bool NodeProcessor::OnBlock(const Block::SystemState::ID& id, const NodeDB::Blob& block, const PeerID& peer)
{
	uint64_t rowid = m_DB.StateFindSafe(id);
	if (!rowid)
		return false;

	if (NodeDB::StateFlags::Functional & m_DB.GetStateFlags(rowid))
		return true;

	NodeDB::Transaction t(m_DB);

	m_DB.SetStateBlock(rowid, block);
	m_DB.SetStateFunctional(rowid);
	m_DB.set_Peer(rowid, &peer);

	if (NodeDB::StateFlags::Reachable & m_DB.GetStateFlags(rowid))
		TryGoUp();

	t.Commit();

	if (NodeDB::StateFlags::Reachable & m_DB.GetStateFlags(rowid))
	{
		NodeDB::StateID sid;
		sid.m_Row = rowid;
		sid.m_Height = id.m_Height;
		FindCongestionPointsAbove(sid);
	}

	return true;
}

bool NodeProcessor::FeedTransaction(Transaction::Ptr&& p)
{
	assert(p);
/*
	NodeDB::StateID sid;
	m_DB.get_Cursor(sid);

	Transaction::Context ctx;
	ctx.m_hMin = ctx.m_hMax = sid.m_Row ? (sid.m_Height+1) : 0; // consider only this block, deny future/past transactions

	if (!p->IsValid(ctx))
		return false;

	if (m_lstCurrentlyMining.empty())
		m_CurrentFees = ctx.m_Fee;
	else
		m_CurrentFees += ctx.m_Fee;
*/
	m_lstCurrentlyMining.push_back(std::move(p));
	return true;
}

template <typename T>
void AppendMoveArray(std::vector<T>& trg, std::vector<T>& src)
{
	trg.reserve(trg.size() + src.size());
	for (size_t i = 0; i < src.size(); i++)
		trg.push_back(std::move(src[i]));
}

struct NodeProcessor::BlockBulder
{
	Block::Body m_Block;
	ECC::Scalar::Native m_Offset;

	void AddOutput(const ECC::Scalar::Native& k, Amount val, bool bCoinbase)
	{
		Output::Ptr pOutp(new Output);
		pOutp->m_Commitment = ECC::Commitment(k, val);
		pOutp->m_Coinbase = bCoinbase;
		pOutp->m_pPublic.reset(new ECC::RangeProof::Public);
		pOutp->m_pPublic->m_Value = val;
		pOutp->m_pPublic->Create(k);
		m_Block.m_vOutputs.push_back(std::move(pOutp));

		ECC::Scalar::Native km = -k;
		m_Offset += km;
	}
};

void NodeProcessor::SimulateMinedBlock(Block::SystemState::Full& s, ByteBuffer& block, ByteBuffer& pow)
{
	NodeDB::Transaction t(m_DB);

	// build the new block on top of the currently reachable blockchain
	NodeDB::StateID sid;
	m_DB.get_Cursor(sid);

	Height h = sid.m_Row ? (sid.m_Height + 1) : 0;
	Amount fee = 0;

	BlockBulder ctxBlock;
	ctxBlock.m_Offset = ECC::Zero;

	ByteBuffer bbRbData;
	RollbackData rbData;
	rbData.m_pUtxo = NULL;

	for (auto it = m_lstCurrentlyMining.begin(); m_lstCurrentlyMining.end() != it; it++)
	{
		Transaction& tx = *(*it);

		if (!tx.m_vInputs.empty())
		{
			size_t nInputs = rbData.m_pUtxo ? rbData.get_Utxos() : 0;
			rbData.m_Buf.resize(rbData.m_Buf.size() + sizeof(RollbackData::Utxo) * tx.m_vInputs.size());
			rbData.m_pUtxo = rbData.get_BufAs() + nInputs;
		}

		Transaction::Context ctx;
		ctx.m_hMin = ctx.m_hMax = h;
		if (tx.IsValid(ctx) && HandleValidatedTx(tx, h, true, rbData))
		{
			fee += ctx.m_Fee;

			AppendMoveArray(ctxBlock.m_Block.m_vInputs, tx.m_vInputs);
			AppendMoveArray(ctxBlock.m_Block.m_vOutputs, tx.m_vOutputs);
			AppendMoveArray(ctxBlock.m_Block.m_vKernelsInput, tx.m_vKernelsInput);
			AppendMoveArray(ctxBlock.m_Block.m_vKernelsOutput, tx.m_vKernelsOutput);

			ctxBlock.m_Offset += ECC::Scalar::Native(tx.m_Offset);
		}
	}
	m_lstCurrentlyMining.clear();

	ECC::Scalar::Native kFee, kCoinbase;
	get_Key(kFee, h, false);

	if (fee)
	{
		ctxBlock.AddOutput(kFee, fee, false);
		verify(HandleBlockElement(*ctxBlock.m_Block.m_vOutputs.back(), h, true));
	} else
	{
		TxKernel::Ptr pKrn(new TxKernel);
		pKrn->m_Excess = ECC::Point::Native(ECC::Context::get().G * kFee);

		ECC::Hash::Value hv;
		pKrn->get_Hash(hv);
		pKrn->m_Signature.Sign(hv, kFee);
		ctxBlock.m_Block.m_vKernelsOutput.push_back(std::move(pKrn));

		verify(HandleBlockElement(*ctxBlock.m_Block.m_vKernelsOutput.back(), true, false)); // Will fail if kernel key duplicated!

		kFee = -kFee;
		ctxBlock.m_Offset += kFee;
	}

	get_Key(kCoinbase, h, true);
	const Amount nCoinbase = Block::s_CoinbaseEmission;
	ctxBlock.AddOutput(kCoinbase, nCoinbase, true);

	verify(HandleBlockElement(*ctxBlock.m_Block.m_vOutputs.back(), h, true));

	// Finalize block construction.
	if (h)
	{
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

	// For test: undo the changes, and then redo, using the newly-created block
	verify(HandleValidatedTx(ctxBlock.m_Block, h, false, rbData));

	t.Commit();

	ctxBlock.m_Block.Sort();
	ctxBlock.m_Block.DeleteIntermediateOutputs();
	ctxBlock.m_Block.m_Offset = ctxBlock.m_Offset;

	Serializer ser;
	ser & ctxBlock.m_Block;
	ser.swap_buf(block);

	OnState(s, NodeDB::Blob(NULL, 0), PeerID());

	Block::SystemState::ID id;
	s.get_ID(id);
	OnBlock(id, block, PeerID());

	OnMined(h, kFee, fee, kCoinbase, nCoinbase);
}

void NodeProcessor::RealizePeerTip(const Block::SystemState::ID& id, const PeerID& peer)
{
	if (!IsRelevantHeight(id.m_Height))
		return;

	if (m_DB.StateFindSafe(id))
		return;

	RequestData(id, false, &peer);
}

} // namespace beam
