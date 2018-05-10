#pragma once

#include "node_processor.h"
#include "../utility/io/timer.h"
#include "../core/proto.h"

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

	struct Processor
		:public NodeProcessor
	{
		// NodeProcessor
		virtual void RequestData(const Block::SystemState::ID&, bool bBlock, const PeerID* pPreferredPeer) override;
		virtual void OnPeerInsane(const PeerID&) override;
		virtual void OnNewState() override;
		virtual void get_Key(ECC::Scalar::Native&, Height h, bool bCoinbase) override;
		virtual void OnMined(Height, const ECC::Scalar::Native& kFee, Amount nFee, const ECC::Scalar::Native& kCoinbase, Amount nCoinbase) override;

		IMPLEMENT_GET_PARENT_OBJ(Node, m_Processor)
	} m_Processor;

	struct Peer;
	typedef std::list<std::unique_ptr<Peer> > PeerList;

	struct State {
		enum Enum {
			Idle,
			Connecting,
			Connected,
			Snoozed,
		};
	};

	struct Peer
		:public proto::NodeConnection
	{
		typedef std::unique_ptr<Peer> Ptr;

		Node* m_pThis;
		PeerList::iterator m_itThis; // iterator to self. Of course better to use intrusive list.

		int m_iPeer; // negative if accepted connection

		State::Enum m_eState;

		io::Timer::Ptr m_pTimer;
		void OnTimer();
		void SetTimer(uint32_t timeout_ms);
		void KillTimer();

		void OnPostError();

		// proto::NodeConnection
		virtual void OnConnected() override;
		virtual void OnClosed(int errorCode) override;
	};

	PeerList m_lstPeers;

	Peer* AllocPeer();

	struct Server
		:public proto::NodeConnection::Server
	{
		// NodeConnection::Server
		virtual void OnAccepted(io::TcpStream::Ptr&&, int errorCode) override;

		IMPLEMENT_GET_PARENT_OBJ(Node, m_Server)
	} m_Server;
};

} // namespace beam
