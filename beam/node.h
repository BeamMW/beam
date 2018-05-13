#pragma once

#include "node_processor.h"
#include "../utility/io/timer.h"
#include "../core/proto.h"
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>

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
			uint32_t m_Reconnect_ms	= 1000;
			uint32_t m_Insane_ms	= 1000 * 3600; // 1 hour
			uint32_t m_GetState_ms	= 1000 * 5;
			uint32_t m_GetBlock_ms	= 1000 * 30;
		} m_Timeout;

	} m_Cfg; // must not be changed after initialization

	~Node();
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

	struct Peer;

	struct Task
		:public boost::intrusive::set_base_hook<>
		,public boost::intrusive::list_base_hook<>
	{
		typedef std::pair<Block::SystemState::ID, bool> Key;
		Key m_Key;

		bool m_bRelevant;
		Peer* m_pOwner;

		bool operator > (const Task& t) const { return (m_Key > t.m_Key); }
	};

	typedef boost::intrusive::list<Task> TaskList;
	typedef boost::intrusive::set<Task, boost::intrusive::compare<std::greater<Task> > > TaskSet;

	TaskList m_lstTasksUnassigned;
	TaskSet m_setTasks;

	void TryAssignTask(Task&, const NodeDB::PeerID*);
	bool ShouldAssignTask(Task&, Peer&);
	void AssignTask(Task&, Peer&);
	void DeleteUnassignedTask(Task&);

	struct Peer
		:public proto::NodeConnection
		,public boost::intrusive::list_base_hook<>
	{
		typedef std::unique_ptr<Peer> Ptr;

		Node* m_pThis;

		int m_iPeer; // negative if accepted connection
		void get_ID(NodeProcessor::PeerID&);

		State::Enum m_eState;

		Height m_TipHeight;

		TaskList m_lstTasks;
		void TakeTasks();
		void ReleaseTasks();
		void ReleaseTask(Task&);
		void SetTimerWrtFirstTask();

		io::Timer::Ptr m_pTimer;
		void OnTimer();
		void SetTimer(uint32_t timeout_ms);
		void KillTimer();

		void OnPostError();
		static void ThrowUnexpected();

		Task& get_FirstTask();
		void OnFirstTaskDone();

		// proto::NodeConnection
		virtual void OnConnected() override;
		virtual void OnClosed(int errorCode) override;
		// messages
		virtual void OnMsg(proto::Ping&&) override;
		virtual void OnMsg(proto::NewTip&&) override;
		virtual void OnMsg(proto::DataMissing&&) override;
		virtual void OnMsg(proto::GetHdr&&) override;
		virtual void OnMsg(proto::Hdr&&) override;
		virtual void OnMsg(proto::GetBody&&) override;
		virtual void OnMsg(proto::Body&&) override;
	};

	typedef boost::intrusive::list<Peer> PeerList;
	PeerList m_lstPeers;

	Peer* AllocPeer();
	void DeletePeer(Peer*);
	Peer* FindPeer(const Processor::PeerID&);

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
