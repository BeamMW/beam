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

void Node::Processor::OnPeerInsane(const PeerID&)
{
}

void Node::Processor::OnNewState()
{
}

void Node::Processor::get_Key(ECC::Scalar::Native&, Height h, bool bCoinbase)
{
}

void Node::Processor::OnMined(Height, const ECC::Scalar::Native& kFee, Amount nFee, const ECC::Scalar::Native& kCoinbase, Amount nCoinbase)
{
}

Node::PeerList::iterator Node::AllocPeer()
{
	Peer::Ptr pPeer(new Peer);

	m_lstPeers.push_back(std::move(pPeer));
	return --m_lstPeers.end();
}

void Node::Initialize()
{
	m_pReactor = io::Reactor::create();

	for (size_t i = 0; i < m_Cfg.m_Connect.size(); i++)
	{
		PeerList::iterator it = AllocPeer();
		Peer& p = *(*it);
		p.m_iPeer = i;
		p.m_pTimer = io::Timer::create(m_pReactor);

		// initiate connect
	}

	// start listen
}

void Node::SetTimer(PeerList::iterator it, uint32_t timeout_ms)
{
	Peer& p = *(*it);
	assert(p.m_pTimer);
	p.m_pTimer->start(timeout_ms, false, [this, it]() { return (this->OnTimer)(it); });
}

void Node::OnTimer(PeerList::iterator it)
{
	Peer& p = *(*it);
}

} // namespace beam