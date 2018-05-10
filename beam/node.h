#pragma once

#include "node_processor.h"
#include "../utility/io/timer.h"

namespace beam
{
struct Node
{
	static const uint16_t s_PortDefault = 31744; // whatever

	struct Config
	{
		io::Address m_Listen;
		std::vector<io::Address> m_Connect;

		std::string m_sPathLocal;
		Height m_Horizon = 1440;

		struct Timeout {
			uint32_t m_Reconnect_ms = 1000;
			uint32_t m_Insane_ms = 1000 * 3600; // 1 hour
		} m_Timeout;

	} m_Cfg; // must not be changed after initialization

	void Initialize();

private:

	io::Reactor::Ptr m_pReactor;

	struct Processor
		:public NodeProcessor
	{
		// NodeProcessor
		virtual void RequestData(const Block::SystemState::ID&, bool bBlock, const PeerID* pPreferredPeer) override;
		virtual void OnPeerInsane(const PeerID&) override;
		virtual void OnNewState() override;
		virtual void get_Key(ECC::Scalar::Native&, Height h, bool bCoinbase) override;
		virtual void OnMined(Height, const ECC::Scalar::Native& kFee, Amount nFee, const ECC::Scalar::Native& kCoinbase, Amount nCoinbase) override;

	} m_Processor;

	struct Peer
	{
		typedef std::unique_ptr<Peer> Ptr;

		int m_iPeer; // negative if accepted connection

		io::Timer::Ptr m_pTimer;
	};

	typedef std::list<Peer::Ptr> PeerList;
	PeerList m_lstPeers;

	PeerList::iterator AllocPeer();

	void OnTimer(PeerList::iterator);
	void SetTimer(PeerList::iterator, uint32_t timeout_ms);
};

} // namespace beam
