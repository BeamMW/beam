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

void Node::Processor::RequestData(const Block::SystemState::ID&, bool bBlock, const PeerID* pPreferredPeer)
{
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

		try {
			(*itThis)->Send(msg);
		} catch (...) {
			(*itThis)->OnPostError();
		}
	}
}

void Node::Processor::get_Key(ECC::Scalar::Native&, Height h, bool bCoinbase)
{
}

void Node::Processor::OnMined(Height, const ECC::Scalar::Native& kFee, Amount nFee, const ECC::Scalar::Native& kCoinbase, Amount nCoinbase)
{
}

Node::Peer* Node::AllocPeer()
{
	Peer::Ptr pPeer(new Peer);
	Peer* pVal = pPeer.get();

	pPeer->m_pTimer = io::Timer::create(io::Reactor::get_Current().shared_from_this());
	m_lstPeers.push_back(std::move(pPeer));

	pVal->m_eState = State::Idle;
	pVal->m_pThis = this;
	pVal->m_itThis = --m_lstPeers.end();

	return pVal;
}

void Node::Initialize()
{
	m_Processor.Initialize(m_Cfg.m_sPathLocal.c_str(), m_Cfg.m_Horizon);

	if (m_Cfg.m_Listen.port())
		m_Server.Listen(m_Cfg.m_Listen);

	for (size_t i = 0; i < m_Cfg.m_Connect.size(); i++)
	{
		Peer* p = AllocPeer();
		p->m_iPeer = i;

		p->OnTimer(); // initiate connect
	}
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

void Node::Peer::OnPostError()
{
	if (m_iPeer < 0)
		m_pThis->m_lstPeers.erase(m_itThis); // will delete this
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
