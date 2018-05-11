#pragma once

#include "node_processor.h"
#include "../utility/io/timer.h"
#include "../core/proto.h"
#include <boost/intrusive/list.hpp>

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
		NodeProcessor::Horizon m_Horizon;

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
		,public boost::intrusive::list_base_hook<>
	{
		typedef std::unique_ptr<Peer> Ptr;

		Node* m_pThis;

		int m_iPeer; // negative if accepted connection
		Height m_TipHeight;
		State::Enum m_eState;

		io::Timer::Ptr m_pTimer;
		void OnTimer();
		void SetTimer(uint32_t timeout_ms);
		void KillTimer();

		void OnPostError();

		// proto::NodeConnection
		virtual void OnConnected() override;
		virtual void OnClosed(int errorCode) override;
		// messages
		virtual void OnMsg(proto::Ping&&) override;
	};

	typedef boost::intrusive::list<Peer> PeerList;
	PeerList m_lstPeers;

	Peer* AllocPeer();
	Peer* FindPeer(const Processor::PeerID&);

	struct CongestionData
	{
		typedef std::pair<Block::SystemState::ID, bool> Key;
		typedef std::map<Key, size_t> Map;

		static const size_t s_Old = 1 << ((sizeof(size_t) << 3) - 1);

		Map m_Data;

	} m_CongestionData;

	void RefreshCongestions();

	struct Server
		:public proto::NodeConnection::Server
	{
		// NodeConnection::Server
		virtual void OnAccepted(io::TcpStream::Ptr&&, int errorCode) override;

		IMPLEMENT_GET_PARENT_OBJ(Node, m_Server)
	} m_Server;
};

} // namespace beam
