#include "node_processor.h"
#include "../utility/serialize.h"
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

	size_t nInp = 0, nOut = 0, nKrn = 0;

	bool bOk = true;
	if (bFwd)
	{
		for ( ; nInp < block.m_vInputs.size(); nInp++)
			if (!HandleBlockElement(*block.m_vInputs[nInp], bFwd))
			{
				bOk = false;
				break;
			}
	} else
	{
		nInp = block.m_vInputs.size();
		nOut = block.m_vOutputs.size();
		nKrn = block.m_vKernels.size();
	}

	if (bFwd && bOk)
	{
		for ( ; nOut < block.m_vOutputs.size(); nOut++)
			if (!HandleBlockElement(*block.m_vOutputs[nOut], sid.m_Height, bFwd))
			{
				bOk = false;
				break;
			}
	}

	if (bFwd && bOk)
	{
		for ( ; nKrn < block.m_vKernels.size(); nKrn++)
			if (!HandleBlockElement(*block.m_vKernels[nKrn], bFwd))
			{
				bOk = false;
				break;
			}
	}

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
	}

	if (!(bFwd && bOk))
	{
		// Rollback all the changes. Must succeed!
		while (nKrn--)
			HandleBlockElement(*block.m_vKernels[nKrn], false);

		while (nOut--)
			HandleBlockElement(*block.m_vOutputs[nOut], sid.m_Height, false);

		while (nInp--)
			HandleBlockElement(*block.m_vInputs[nInp], bFwd);
	}

	if (bOk && bFirstTime)
		m_DB.SetFlags(sid.m_Row, nFlags | NodeDB::StateFlags::BlockPassed);

	return bOk;
}

bool NodeProcessor::HandleBlockElement(const Input& v, bool bFwd)
{
	UtxoTree::Key::Formatted kf;
	kf.m_Commitment = v.m_Commitment;
	kf.m_Height = v.m_Height;
	kf.m_bCoinbase = v.m_Coinbase;
	kf.m_bConfidential = v.m_Confidential;

	UtxoTree::Key key;
	key = kf;

	UtxoTree::Cursor cu;
	bool bCreate = !bFwd;
	UtxoTree::MyLeaf* p = m_Utxos.Find(cu, key, bCreate);

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
	UtxoTree::Key::Formatted kf;
	kf.m_Commitment = v.m_Commitment;
	kf.m_Height = h;
	kf.m_bCoinbase = v.m_Coinbase;
	kf.m_bConfidential = v.m_pConfidential != NULL;

	UtxoTree::Key key;
	key = kf;
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

			m_DB.AddUtxo(blob, NodeDB::Blob(sb.first, sb.second), 1, 1);
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

		m_DB.AddKernel(NodeDB::Blob(hv.m_pData, sizeof(hv.m_pData)), NodeDB::Blob(sb.first, sb.second), true);
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

} // namespace beam
