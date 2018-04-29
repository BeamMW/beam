#include "node_processor.h"

namespace beam {

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
				throw "oops";

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
				throw "oops";

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

			if (!GoForward(sidPos))
			{
				bPathOk = false;
				break;
			}
		}

		if (bPathOk)
			break; // at position
	}
}

bool NodeProcessor::GoForward(const NodeDB::StateID& sid)
{
	m_DB.MoveFwd(sid);
	return true;
}

void NodeProcessor::Rollback(const NodeDB::StateID& sid)
{
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

void NodeProcessor::OnState(const Block::SystemState::Full&, const Block::PoW&, const PeerID&)
{
}

bool NodeProcessor::OnBlock(const Block::SystemState::ID&, const Block::Body&, const PeerID&)
{
	return true;
}

} // namespace beam
