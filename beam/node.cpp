#include "node.h"
#include "../core/serialization_adapters.h"
#include "../core/proto.h"

#include "../p2p/protocol.h"
#include "../p2p/connection.h"
#define LOG_DEBUG_ENABLED 0
#include "../utility/logger.h"
#include "../utility/bridge.h"
#include "../utility/io/tcpserver.h"

namespace beam {

void Node::RefreshCongestions()
{
	for (TaskSet::iterator it = m_setTasks.begin(); m_setTasks.end() != it; it++)
		it->m_bRelevant = false;

	m_Processor.EnumCongestions();

	for (TaskSet::iterator it = m_setTasks.begin(); m_setTasks.end() != it; )
	{
		TaskSet::iterator itThis = it++;
		if (!itThis->m_bRelevant && !itThis->m_pOwner)
			DeleteUnassignedTask(*it);
	}
}

void Node::DeleteUnassignedTask(Task& t)
{
	assert(!t.m_pOwner);
	m_lstTasksUnassigned.erase(TaskList::s_iterator_to(t));
	m_setTasks.erase(TaskSet::s_iterator_to(t));
	delete &t;
}

void Node::TryAssignTask(Task& t, const NodeDB::PeerID* pPeerID)
{
}

void Node::Processor::RequestData(const Block::SystemState::ID& id, bool bBlock, const PeerID* pPreferredPeer)
{
	Task tKey;
	tKey.m_Key.first = id;
	tKey.m_Key.second = bBlock;

	TaskSet::iterator it = get_ParentObj().m_setTasks.find(tKey);
	if (get_ParentObj().m_setTasks.end() == it)
	{
		Task* pTask = new Task;
		pTask->m_Key = tKey.m_Key;
		pTask->m_bRelevant = true;
		pTask->m_pOwner = NULL;

		get_ParentObj().m_setTasks.insert(*pTask);
		get_ParentObj().m_lstTasksUnassigned.push_back(*pTask);

		get_ParentObj().TryAssignTask(*pTask, pPreferredPeer);

	} else
		it->m_bRelevant = true;
}

void Node::Processor::OnPeerInsane(const PeerID& peerID)
{
	Peer* pPeer = get_ParentObj().FindPeer(peerID);
	if (pPeer && (State::Snoozed != pPeer->m_eState))
		pPeer->OnClosed(-1);
}

void Node::Processor::OnNewState()
{
	proto::NewTip msg;
	if (!get_CurrentState(msg.m_ID))
		return;

	for (PeerList::iterator it = get_ParentObj().m_lstPeers.begin(); get_ParentObj().m_lstPeers.end() != it; )
	{
		PeerList::iterator itThis = it;
		it++;
		Peer& peer = *itThis;

		try {
			if (peer.m_TipHeight <= msg.m_ID.m_Height)
				peer.Send(msg);
		} catch (...) {
			peer.OnPostError();
		}
	}

	get_ParentObj().RefreshCongestions();
}

void Node::Processor::get_Key(ECC::Scalar::Native&, Height h, bool bCoinbase)
{
}

void Node::Processor::OnMined(Height, const ECC::Scalar::Native& kFee, Amount nFee, const ECC::Scalar::Native& kCoinbase, Amount nCoinbase)
{
}

Node::Peer* Node::AllocPeer()
{
	Peer* pPeer = new Peer;
	m_lstPeers.push_back(*pPeer);

	pPeer->m_pTimer = io::Timer::create(io::Reactor::get_Current().shared_from_this());

	pPeer->m_eState = State::Idle;
	pPeer->m_pThis = this;

	return pPeer;
}

void Node::DeletePeer(Peer* p)
{
	m_lstPeers.erase(PeerList::s_iterator_to(*p));
	delete p;
}

Node::Peer* Node::FindPeer(const Processor::PeerID& peerID)
{
	// current interpretation (naive): just assume that peerID is the index
	for (PeerList::iterator it = m_lstPeers.begin(); m_lstPeers.end() != it; it++)
	{
		Processor::PeerID id2;
		id2 = (uint32_t) it->m_iPeer;
		if (peerID == id2)
			return &*it;
	}
	return NULL;
}

void Node::Initialize()
{
	m_Processor.m_Horizon = m_Cfg.m_Horizon;
	m_Processor.Initialize(m_Cfg.m_sPathLocal.c_str());

	RefreshCongestions();

	if (m_Cfg.m_Listen.port())
		m_Server.Listen(m_Cfg.m_Listen);

	for (size_t i = 0; i < m_Cfg.m_Connect.size(); i++)
	{
		Peer* p = AllocPeer();
		p->m_iPeer = i;

		p->OnTimer(); // initiate connect
	}
}

Node::~Node()
{
	while (!m_lstPeers.empty())
	{
		Peer& p = m_lstPeers.front();
		p.ReleaseTasks();
		DeletePeer(&p);
	}

	while (!m_lstTasksUnassigned.empty())
		DeleteUnassignedTask(m_lstTasksUnassigned.front());

	assert(m_setTasks.empty());
}

void Node::Peer::SetTimer(uint32_t timeout_ms)
{
	assert(m_pTimer);
	m_pTimer->start(timeout_ms, false, [this]() { return (this->OnTimer)(); });
}

void Node::Peer::KillTimer()
{
	assert(m_pTimer);
	m_pTimer->cancel();
}

void Node::Peer::OnTimer()
{
	switch (m_eState)
	{
	case State::Idle:
	case State::Snoozed:

		assert(m_iPeer >= 0);

		try {
			Connect(m_pThis->m_Cfg.m_Connect[m_iPeer]);
		} catch (...) {
			OnPostError();
		}

		break;
	}
}

void Node::Peer::OnConnected()
{
	if (State::Connecting == m_eState)
		KillTimer();
	else
		assert(State::Idle == m_eState);

	m_eState = State::Connected;
	m_TipHeight = 0;

	proto::NewTip msg;
	if (m_pThis->m_Processor.get_CurrentState(msg.m_ID))
		Send(msg);
}

void Node::Peer::OnClosed(int errorCode)
{
	assert(State::Connected == m_eState);
	if (-1 == errorCode) // protocol error
		m_eState = State::Snoozed;
	OnPostError();
}

void Node::Peer::ReleaseTasks()
{
	while (!m_lstTasks.empty())
	{
		Task& t = m_lstTasks.front();
		m_lstTasks.pop_front();

		assert(this == t.m_pOwner);
		t.m_pOwner = NULL;

		m_pThis->m_lstTasksUnassigned.push_back(t);

		if (t.m_bRelevant)
			m_pThis->TryAssignTask(t, NULL);
		else
			m_pThis->DeleteUnassignedTask(t);
	}
}

void Node::Peer::OnPostError()
{
	ReleaseTasks();

	if (m_iPeer < 0)
		m_pThis->DeletePeer(this);
	else
	{
		Reset(); // connection layer

		if (State::Snoozed == m_eState)
			SetTimer(m_pThis->m_Cfg.m_Timeout.m_Insane_ms);
		else
		{
			m_eState = State::Idle;
			SetTimer(m_pThis->m_Cfg.m_Timeout.m_Reconnect_ms);
		}
	}
}

void Node::Peer::OnMsg(proto::Ping&&)
{
	proto::Pong msg;
	Send(msg);
}

void Node::Server::OnAccepted(io::TcpStream::Ptr&& newStream, int errorCode)
{
	if (newStream)
	{
		Peer* p = get_ParentObj().AllocPeer();
		p->m_iPeer = -1;

		p->Accept(std::move(newStream));
		p->OnConnected();
	}
}

} // namespace beam
