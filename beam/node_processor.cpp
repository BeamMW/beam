#include "node_processor.h"
#include "../utility/serialize.h"
#include "../core/ecc_native.h"
#include "../core/serialization_adapters.h"

namespace beam {

void NodeProcessor::OnCorrupted()
{
	throw std::runtime_error("node data corrupted");
}

void NodeProcessor::Initialize(const char* szPath, Height horizon)
{
	m_DB.Open(szPath);
	m_Horizon = horizon;

	// Load all th 'live' data
	{
		NodeDB::WalkerUtxo wutxo(m_DB);
		for (m_DB.EnumLiveUtxos(wutxo); wutxo.MoveNext(); )
		{
			assert(wutxo.m_nUnspentCount);

			if (UtxoTree::Key::s_Bytes != wutxo.m_Key.n)
				OnCorrupted();

			static_assert(sizeof(UtxoTree::Key) == UtxoTree::Key::s_Bytes, "");
			const UtxoTree::Key& key = *(UtxoTree::Key*) wutxo.m_Key.p;

			UtxoTree::Cursor cu;
			bool bCreate = true;

			m_Utxos.Find(cu, key, bCreate)->m_Value.m_Count = wutxo.m_nUnspentCount;
			assert(bCreate);
		}
	}

	{
		NodeDB::WalkerKernel wkrn(m_DB);
		for (m_DB.EnumLiveKernels(wkrn); wkrn.MoveNext(); )
		{
			if (sizeof(Merkle::Hash) != wkrn.m_Key.n)
				OnCorrupted();

			const Merkle::Hash& key = *(Merkle::Hash*) wkrn.m_Key.p;

			RadixHashOnlyTree::Cursor cu;
			bool bCreate = true;

			m_Kernels.Find(cu, key, bCreate);
			assert(bCreate);
		}
	}

	NodeDB::Transaction t(m_DB);
	TryGoUp();
	t.Commit();
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
	}
}

void NodeProcessor::PruneOld(Height h)
{
	if (h <= m_Horizon)
		return;
	h -= m_Horizon;

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
}

bool NodeProcessor::HandleBlock(const NodeDB::StateID& sid, PeerID& peer, bool bFwd)
{
	ByteBuffer bb;
	m_DB.GetStateBlock(sid.m_Row, bb, peer);

	uint32_t nFlags;
	bool bFirstTime;

	if (bFwd)
	{
		nFlags = m_DB.GetStateFlags(sid.m_Row);
		bFirstTime = !(NodeDB::StateFlags::BlockPassed & nFlags);

	} else
		bFirstTime = false;

	Block::SystemState::Full s;

	if (bFirstTime)
	{
		m_DB.get_State(sid.m_Row, s);

		Merkle::Proof proof;
		m_DB.get_Proof(proof, sid, sid.m_Height);

		Merkle::Interpret(s.m_Prev, proof);
		if (s.m_States != s.m_Prev)
			return false; // The state (even the header) is formed incorrectly!
	}

	Block::Body block;
	try {

		Deserializer der;
		der.reset(bb.empty() ? NULL :& bb.at(0), bb.size());
		der & block;
	} catch (const std::exception&) {
		return false;
	}

	bb.clear();

	if (bFirstTime && !block.IsValid(sid.m_Height, sid.m_Height))
		return false;

	bool bOk = HandleValidatedTx(block, sid.m_Height, bFwd, false);

	if (bFirstTime && bOk)
	{
		// check the validity of state description.
		Merkle::Hash hv;
		m_Utxos.get_Hash(hv);
		if (s.m_Utxos != hv)
			bOk = false;

		m_Kernels.get_Hash(hv);
		if (s.m_Kernels != hv)
			bOk = false;

		if (bOk)
			m_DB.SetFlags(sid.m_Row, nFlags | NodeDB::StateFlags::BlockPassed);
		else
			HandleValidatedTx(block, sid.m_Height, false, false);
	}

	return bOk;
}

bool NodeProcessor::HandleValidatedTx(const TxBase& tx, Height h, bool bFwd, bool bAutoAdjustInp)
{
	size_t nInp = 0, nOut = 0, nKrn = 0;

	bool bOk = true;
	if (bFwd)
	{
		for ( ; nInp < tx.m_vInputs.size(); nInp++)
			if (!HandleBlockElement(*tx.m_vInputs[nInp], bFwd, bAutoAdjustInp))
			{
				bOk = false;
				break;
			}
	} else
	{
		nInp = tx.m_vInputs.size();
		nOut = tx.m_vOutputs.size();
		nKrn = tx.m_vKernels.size();
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
		for ( ; nKrn < tx.m_vKernels.size(); nKrn++)
			if (!HandleBlockElement(*tx.m_vKernels[nKrn], bFwd))
			{
				bOk = false;
				break;
			}
	}

	if (!(bFwd && bOk))
	{
		// Rollback all the changes. Must succeed!
		while (nKrn--)
			HandleBlockElement(*tx.m_vKernels[nKrn], false);

		while (nOut--)
			HandleBlockElement(*tx.m_vOutputs[nOut], h, false);

		while (nInp--)
			HandleBlockElement(*tx.m_vInputs[nInp], false, false);
	}

	return bOk;
}

bool NodeProcessor::HandleBlockElement(Input& v, bool bFwd, bool bAutoAdjustInp)
{
	UtxoTree::Key key;
	key = v;

	UtxoTree::Cursor cu;
	bool bCreate = !bFwd;
	UtxoTree::MyLeaf* p = m_Utxos.Find(cu, key, bCreate);

	if (!p && bAutoAdjustInp)
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
		if (!UtxoTree::Traverse(cu, t))
		{
			Input v2;
			t.m_pLeaf->m_Key.ToID(v2);

			if (v.m_Commitment == v2.m_Commitment)
			{
				// Found!
				p = t.m_pLeaf;
				v = v2; // adjust
				key = t.m_pLeaf->m_Key;
			}
		}

	}


	if (!p)
		return false; // attempt to spend a non-existing UTXO!

	cu.Invalidate();

	if (bFwd)
	{
		assert(p->m_Value.m_Count); // we don't store zeroes

		if (! --p->m_Value.m_Count)
			m_Utxos.Delete(cu);
	} else
	{
		if (bCreate)
			p->m_Value.m_Count = 1;
		else
			p->m_Value.m_Count++;
	}

	m_DB.ModifyUtxo(NodeDB::Blob(&key, sizeof(key)), 0, bFwd ? -1 : 1);
	return true;
}

bool NodeProcessor::HandleBlockElement(const Output& v, Height h, bool bFwd)
{
	UtxoID utxoid;
	v.get_ID(utxoid, h);

	UtxoTree::Key key;
	key = utxoid;
	NodeDB::Blob blob(&key, sizeof(key));

	UtxoTree::Cursor cu;
	bool bCreate = true;
	UtxoTree::MyLeaf* p = m_Utxos.Find(cu, key, bCreate);

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

			m_DB.AddUtxo(blob, NodeDB::Blob(sb.first, (uint32_t) sb.second), 1, 1);
		}
		else
		{
			p->m_Value.m_Count++;
			m_DB.ModifyUtxo(blob, 0, 1);
		}
	} else
	{
		if (1 == p->m_Value.m_Count)
		{
			m_Utxos.Delete(cu);
			m_DB.DeleteUtxo(blob);
		}
		else
		{
			p->m_Value.m_Count--;
			m_DB.ModifyUtxo(blob, 0, -1);
		}
	}

	return true;
}

bool NodeProcessor::HandleBlockElement(const TxKernel& v, bool bFwd)
{
	Merkle::Hash hv;
	v.get_Hash(hv);
	ECC::Hash::Processor() << hv << v.m_Excess >> hv; // add the public excess, it's not included by default


	RadixHashOnlyTree::Cursor cu;
	bool bCreate = true;
	RadixHashOnlyTree::MyLeaf* p = m_Kernels.Find(cu, hv, bCreate);

	if (bFwd)
	{
		if (!bCreate)
			return false; // attempt to use the same exactly kernel twice. This should be banned!

		Serializer ser;
		ser & v;
		SerializeBuffer sb = ser.buffer();

		m_DB.AddKernel(NodeDB::Blob(hv.m_pData, sizeof(hv.m_pData)), NodeDB::Blob(sb.first, (uint32_t) sb.second), true);
	} else
	{
		m_Kernels.Delete(cu);
		m_DB.DeleteKernel(NodeDB::Blob(hv.m_pData, sizeof(hv.m_pData)));
	}
	return true;
}

bool NodeProcessor::GoForward(const NodeDB::StateID& sid)
{
	PeerID peer;
	if (HandleBlock(sid, peer, true))
	{
		m_DB.MoveFwd(sid);
		return true;
	}

	m_DB.DelStateBlock(sid.m_Row);
	m_DB.SetStateNotFunctional(sid.m_Row);

	OnPeerInsane(peer);
	return false;
}

void NodeProcessor::Rollback(const NodeDB::StateID& sid)
{
	PeerID peer;
	if (!HandleBlock(sid, peer, false))
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

bool NodeProcessor::OnState(const Block::SystemState::Full& s, const NodeDB::Blob& /*pow*/, const PeerID& peer)
{
	Block::SystemState::ID id;
	s.get_ID(id);
	if (m_DB.StateFindSafe(id))
		return true;

	NodeDB::StateID sid;
	if (m_DB.get_Cursor(sid) && (sid.m_Height > m_Horizon) && (sid.m_Height - m_Horizon > s.m_Height))
		return false;

	NodeDB::Transaction t(m_DB);
	m_DB.InsertState(s);

	t.Commit();

	// request bodies?


	return true;
}

bool NodeProcessor::OnBlock(const Block::SystemState::ID& id, const NodeDB::Blob& block, const PeerID& peer)
{
	uint64_t rowid = m_DB.StateFindSafe(id);
	if (!rowid)
		return false;

	uint32_t nFlags = m_DB.GetStateFlags(rowid);
	if (NodeDB::StateFlags::Functional & nFlags)
		return true;

	NodeDB::Transaction t(m_DB);

	m_DB.SetStateBlock(rowid, block, peer);
	m_DB.SetStateFunctional(rowid);

	TryGoUp();

	t.Commit();

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

	for (auto it = m_lstCurrentlyMining.begin(); m_lstCurrentlyMining.end() != it; it++)
	{
		Transaction& tx = *(*it);

		Transaction::Context ctx;
		ctx.m_hMin = ctx.m_hMax = h;
		if (tx.IsValid(ctx) && HandleValidatedTx(tx, h, true, true))
		{
			fee += ctx.m_Fee;

			AppendMoveArray(ctxBlock.m_Block.m_vInputs, tx.m_vInputs);
			AppendMoveArray(ctxBlock.m_Block.m_vOutputs, tx.m_vOutputs);
			AppendMoveArray(ctxBlock.m_Block.m_vKernels, tx.m_vKernels);

			ctxBlock.m_Offset += ECC::Scalar::Native(tx.m_Offset);
		}
	}
	m_lstCurrentlyMining.clear();

	ECC::Scalar::Native kFee, kCoinbase;

	if (fee)
	{
		get_Key(kFee, h, false);
		ctxBlock.AddOutput(kFee, fee, false);

		verify(HandleBlockElement(*ctxBlock.m_Block.m_vOutputs.back(), h, true));
	}

	get_Key(kCoinbase, h, true);
	const Amount nCoinbase = Block::s_CoinbaseEmission;
	ctxBlock.AddOutput(kCoinbase, nCoinbase, true);

	verify(HandleBlockElement(*ctxBlock.m_Block.m_vOutputs.back(), h, true));

	ctxBlock.m_Block.Sort();
	ctxBlock.m_Block.DeleteIntermediateOutputs(h);
	ctxBlock.m_Block.m_Offset = ctxBlock.m_Offset;

	// Finalize block construction.
	if (h)
	{
		m_DB.get_State(sid.m_Row, s);
		s.get_Hash(s.m_Prev);
		m_DB.get_PredictedStatesHash(s.m_States, sid);
	}
	else
	{
		ZeroObject(s.m_Prev);
		ZeroObject(s.m_States);
	}

	s.m_Height = h;

	m_Utxos.get_Hash(s.m_Utxos);
	m_Kernels.get_Hash(s.m_Kernels);

	Serializer ser;
	ser & ctxBlock.m_Block;
	ser.swap_buf(block);

	// For test: undo the changes, and then redo, using the newly-created block
	verify(HandleValidatedTx(ctxBlock.m_Block, h, false, false));

	t.Commit();

	OnState(s, NodeDB::Blob(NULL, 0), PeerID());

	Block::SystemState::ID id;
	s.get_ID(id);
	OnBlock(id, NodeDB::Blob(&block.at(0), (uint32_t) block.size()), PeerID());

	OnMined(h, kFee, fee, kCoinbase, nCoinbase);
}

} // namespace beam
