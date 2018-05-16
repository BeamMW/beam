#include "node.h"
#include "../core/serialization_adapters.h"
#include "../core/proto.h"
#include "../core/ecc_native.h"

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

	for (TaskList::iterator it = m_lstTasksUnassigned.begin(); m_lstTasksUnassigned.end() != it; )
	{
		TaskList::iterator itThis = it++;
		if (!itThis->m_bRelevant)
			DeleteUnassignedTask(*itThis);
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
	while (true)
	{
		Peer* pBestMatch = NULL;

		for (PeerList::iterator it = m_lstPeers.begin(); m_lstPeers.end() != it; it++)
		{
			Peer& p = *it;
			if (pBestMatch)
			{
				assert(pPeerID);

				NodeDB::PeerID id;
				p.get_ID(id);
				if (id != *pPeerID)
					continue;
			}

			if (ShouldAssignTask(t, p))
			{
				pBestMatch = &p;
				if (!pPeerID)
					break;
			}
		}

		if (!pBestMatch)
			break;

		try {
			AssignTask(t, *pBestMatch);
			return; // done

		} catch (...) {
			pBestMatch->OnPostError();
		}
	}
}

void Node::AssignTask(Task& t, Peer& p)
{
	if (t.m_Key.second)
	{
		proto::GetBody msg;
		msg.m_ID = t.m_Key.first;
		p.Send(msg);
	}
	else
	{
		proto::GetHdr msg;
		msg.m_ID = t.m_Key.first;
		p.Send(msg);
	}

	bool bEmpty = p.m_lstTasks.empty();

	assert(!t.m_pOwner);
	t.m_pOwner = &p;

	m_lstTasksUnassigned.erase(TaskList::s_iterator_to(t));
	p.m_lstTasks.push_back(t);

	if (bEmpty)
		p.SetTimerWrtFirstTask();
}

void Node::Peer::SetTimerWrtFirstTask()
{
	if (m_lstTasks.empty())
		KillTimer();
	else
		SetTimer(m_lstTasks.front().m_Key.second ? m_pThis->m_Cfg.m_Timeout.m_GetBlock_ms : m_pThis->m_Cfg.m_Timeout.m_GetState_ms);
}

bool Node::ShouldAssignTask(Task& t, Peer& p)
{
	if (State::Connected != p.m_eState)
		return false;

	if (p.m_TipHeight < t.m_Key.first.m_Height)
		return false;

	// check if the peer currently transfers a block
	for (TaskList::iterator it = p.m_lstTasks.begin(); p.m_lstTasks.end() != it; it++)
		if (it->m_Key.second)
			return false;

	return p.m_setRejected.end() == p.m_setRejected.find(t.m_Key);
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
			if ((State::Connected == peer.m_eState) && (peer.m_TipHeight <= msg.m_ID.m_Height))
				peer.Send(msg);
		} catch (...) {
			peer.OnPostError();
		}
	}

	get_ParentObj().RefreshCongestions();
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
	for (PeerList::iterator it = m_lstPeers.begin(); m_lstPeers.end() != it; it++)
		it->m_eState = State::Snoozed; // prevent re-assigning of tasks in the next loop

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

	case State::Connected:

		assert(!m_lstTasks.empty()); // task (request) wasn't handled in time
		OnPostError();
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

void Node::Peer::get_ID(NodeProcessor::PeerID& id)
{
	id = (size_t) m_iPeer;
}

void Node::Peer::ReleaseTasks()
{
	while (!m_lstTasks.empty())
		ReleaseTask(m_lstTasks.front());
}

void Node::Peer::ReleaseTask(Task& t)
{
	assert(this == t.m_pOwner);
	t.m_pOwner = NULL;

	m_lstTasks.erase(TaskList::s_iterator_to(t));
	m_pThis->m_lstTasksUnassigned.push_back(t);

	if (t.m_bRelevant)
		m_pThis->TryAssignTask(t, NULL);
	else
		m_pThis->DeleteUnassignedTask(t);
}

void Node::Peer::OnPostError()
{
	if (State::Snoozed != m_eState)
		m_eState = State::Idle; // prevent reassigning the tasks

	ReleaseTasks();

	if (m_iPeer < 0)
		m_pThis->DeletePeer(this);
	else
	{
		Reset(); // connection layer
		m_setRejected.clear();

		if (State::Snoozed == m_eState)
			SetTimer(m_pThis->m_Cfg.m_Timeout.m_Insane_ms);
		else
		{
			m_eState = State::Idle;
			SetTimer(m_pThis->m_Cfg.m_Timeout.m_Reconnect_ms);
		}
	}
}

void Node::Peer::TakeTasks()
{
	for (TaskList::iterator it = m_pThis->m_lstTasksUnassigned.begin(); m_pThis->m_lstTasksUnassigned.end() != it; )
	{
		Task& t = *it;
		it++;

		if (m_pThis->ShouldAssignTask(t, *this))
			m_pThis->AssignTask(t, *this);
	}
}

void Node::Peer::OnMsg(proto::Ping&&)
{
	proto::Pong msg;
	Send(msg);
}

void Node::Peer::OnMsg(proto::NewTip&& msg)
{
	if (msg.m_ID.m_Height < m_TipHeight)
	{
		OnClosed(-1); // insane!
		return;
	}

	m_TipHeight = msg.m_ID.m_Height;
	m_setRejected.clear();

	TakeTasks();

	if (m_pThis->m_Processor.IsStateNeeded(msg.m_ID))
	{
		NodeProcessor::PeerID id;
		get_ID(id);
		m_pThis->m_Processor.RequestData(msg.m_ID, false, &id);
	}
}

void Node::Peer::ThrowUnexpected()
{
	throw std::runtime_error("unexpected");
}

Node::Task& Node::Peer::get_FirstTask()
{
	if (m_lstTasks.empty())
		ThrowUnexpected();
	return m_lstTasks.front();
}

void Node::Peer::OnFirstTaskDone()
{
	ReleaseTask(get_FirstTask());
	SetTimerWrtFirstTask();
}

void Node::Peer::OnMsg(proto::DataMissing&&)
{
	Task& t = get_FirstTask();
	m_setRejected.insert(t.m_Key);

	OnFirstTaskDone();
}

void Node::Peer::OnMsg(proto::GetHdr&& msg)
{
	uint64_t rowid = m_pThis->m_Processor.get_DB().StateFindSafe(msg.m_ID);
	if (rowid)
	{
		proto::Hdr msgHdr;
		m_pThis->m_Processor.get_DB().get_State(rowid, msgHdr.m_Description);
		Send(msgHdr);
	} else
	{
		proto::DataMissing msgMiss;
		Send(msgMiss);
	}
}

void Node::Peer::OnMsg(proto::Hdr&& msg)
{
	Task& t = get_FirstTask();

	if (t.m_Key.second)
		ThrowUnexpected();

	Block::SystemState::ID id;
	msg.m_Description.get_ID(id);
	if (id != t.m_Key.first)
		ThrowUnexpected();

	// uncomment this when blocks with valid PoW are generated
	if (!m_pThis->m_Cfg.m_bDontVerifyPoW && !msg.m_Description.IsValidPoW())
		ThrowUnexpected();

	t.m_bRelevant = false;
	OnFirstTaskDone();

	NodeDB::PeerID pid;
	get_ID(pid);

	if (m_pThis->m_Processor.OnState(msg.m_Description, pid))
		m_pThis->RefreshCongestions(); // NOTE! Can call OnPeerInsane()
}

void Node::Peer::OnMsg(proto::GetBody&& msg)
{
	uint64_t rowid = m_pThis->m_Processor.get_DB().StateFindSafe(msg.m_ID);
	if (rowid)
	{
		proto::Body msgBody;
		ByteBuffer bbRollback;
		m_pThis->m_Processor.get_DB().GetStateBlock(rowid, msgBody.m_Buffer, bbRollback);

		if (!msgBody.m_Buffer.empty())
		{
			Send(msgBody);
			return;
		}

	}

	proto::DataMissing msgMiss;
	Send(msgMiss);
}

void Node::Peer::OnMsg(proto::Body&& msg)
{
	Task& t = get_FirstTask();

	if (!t.m_Key.second)
		ThrowUnexpected();

	Block::SystemState::ID id = t.m_Key.first;

	t.m_bRelevant = false;
	OnFirstTaskDone();

	NodeDB::Blob pow(NULL, 0); // TODO
	NodeDB::PeerID pid;
	get_ID(pid);

	if (m_pThis->m_Processor.OnBlock(id, msg.m_Buffer, pid))
		m_pThis->RefreshCongestions(); // NOTE! Can call OnPeerInsane()
}

void Node::Peer::OnMsg(proto::NewTransaction&& msg)
{
	Send(proto::Boolean{ true });
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
