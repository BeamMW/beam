// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "processor.h"
#include "utility/io/timer.h"
#include "core/proto.h"
#include "core/block_crypt.h"
#include "core/peer_manager.h"
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>
#include <condition_variable>
#include <pow/external_pow.h>

namespace beam
{

struct Node
{
	static const uint16_t s_PortDefault = 31744; // whatever

	struct IObserver
	{
		virtual void OnSyncProgress() = 0;
		virtual void OnStateChanged() {}
		virtual void OnRolledBack(const Block::SystemState::ID& id) {};
		virtual void InitializeUtxosProgress(uint64_t done, uint64_t total) {};

        enum Error
        {
			Unknown,
			TimeDiffToLarge
        };

        virtual void OnSyncError(Error error = Unknown) {}
	};

	struct Config
	{
		io::Address m_Listen;
		uint16_t m_BeaconPort = 0; // set to 0 if should use the same port for listen
		uint32_t m_BeaconPeriod_ms = 500;
		std::vector<io::Address> m_Connect;

		std::string m_sPathLocal;
		NodeProcessor::Horizon m_Horizon;

		struct Timeout {
			uint32_t m_GetState_ms	= 1000 * 5;
			uint32_t m_GetBlock_ms	= 1000 * 30;
			uint32_t m_GetTx_ms		= 1000 * 5;
			uint32_t m_GetBbsMsg_ms	= 1000 * 10;
			uint32_t m_MiningSoftRestart_ms = 1000;
			uint32_t m_TopPeersUpd_ms = 1000 * 60 * 10; // once in 10 minutes
			uint32_t m_PeersUpdate_ms	= 1000; // reconsider every second
			uint32_t m_PeersDbFlush_ms = 1000 * 60; // 1 minute
		} m_Timeout;

		uint32_t m_MaxConcurrentBlocksRequest = 18;
		uint32_t m_MaxPoolTransactions = 100 * 1000;
		uint32_t m_MiningThreads = 0; // by default disabled

		bool m_LogUtxos = false; // may be insecure. Off by default.
		bool m_LogTxStem = true;
		bool m_LogTxFluff = true;

		// Number of verification threads for CPU-hungry cryptography. Currently used for block validation only.
		// 0: single threaded
		// negative: number of cores minus number of mining threads.
		int m_VerificationThreads = 0;

		struct Bbs
		{
			uint32_t m_MessageTimeout_s = 3600 * 12; // 1/2 day
			uint32_t m_CleanupPeriod_ms = 3600 * 1000; // 1 hour

			NodeDB::BbsTotals m_Limit;

			Bbs()
			{
				// set the following to 0 to disable BBS replication.
				// Typically each transaction demands several messages, there're roughly max ~1K txs per block, and 1 block per minute.
				// Means, for the default 12-hour lifetime it's about 1.5 mln, hence the following (20 mln) is more than enough
				m_Limit.m_Count = 20000000;
				// max bbs msg size is proto::Bbs::s_MaxMsgSize == 1Mb. However mostly they're much smaller.
				m_Limit.m_Size = uint64_t(5) * 1024U * 1024U * 1024U; // 5Gb
			}

			bool IsEnabled() const { return m_Limit.m_Count > 0; }

		} m_Bbs;

		struct BandwidthCtl
		{
			size_t m_Chocking = 1024 * 1024;
			size_t m_Drown    = 1024*1024 * 20;

			size_t m_MaxBodyPackSize = 1024 * 1024 * 5;
			uint32_t m_MaxBodyPackCount = 3000;

		} m_BandwidthCtl;

		struct TestMode {
			// for testing only!
			uint32_t m_FakePowSolveTime_ms = 15 * 1000;

		} m_TestMode;

		ByteBuffer m_Treasury; // needed only for the 1st run

		struct Dandelion
		{
			uint16_t m_FluffProbability = 0x1999; // normalized wrt 16 bit. Equals to 0.1
			uint32_t m_TimeoutMin_ms = 20000;
			uint32_t m_TimeoutMax_ms = 50000;

			uint32_t m_AggregationTime_ms = 10000;
			uint32_t m_OutputsMin = 5; // must be aggregated.
			uint32_t m_OutputsMax = 40; // may be aggregated

			// dummy creation strategy
			uint32_t m_DummyLifetimeLo = 720;
			uint32_t m_DummyLifetimeHi = 1440 * 7; // set to 0 to disable

		} m_Dandelion;

		struct Recovery
		{
			std::string m_sPathOutput; // directory with (back)slash and optionally a common prefix
			uint32_t m_Granularity = 30; // block interval for newer recovery generation

		} m_Recovery;

		NodeProcessor::StartParams m_ProcessorParams;

		IObserver* m_Observer = nullptr;

	} m_Cfg; // must not be changed after initialization

	struct Keys
	{
		// There following Ptrs may point to the same object.

		Key::IKdf::Ptr m_pGeneric; // used for internal nonce generation. Auto-generated from system random if not specified
		Key::IPKdf::Ptr m_pOwner; // used for wallet authentication and UTXO tagging (this is the master view key)
		Key::IKdf::Ptr m_pMiner; // if not set - offline mining and decoy creation would be impossible

		Key::Index m_nMinerSubIndex = 0;

		void InitSingleKey(const ECC::uintBig& seed);
		void SetSingleKey(const Key::IKdf::Ptr&);

	} m_Keys;

	~Node();
	void Initialize(IExternalPOW* externalPOW=nullptr);

	NodeProcessor& get_Processor() { return m_Processor; } // for tests only!

	struct SyncStatus
	{
		static const uint32_t s_WeightHdr = 1;
		static const uint32_t s_WeightBlock = 8;

		// in units of Height, but different.
		Height m_Done;
		Height m_Total;

		bool operator == (const SyncStatus&) const;

		void ToRelative(Height hDone0);

	} m_SyncStatus;

	uint32_t get_AcessiblePeerCount() const; // all the peers with known addresses. Including temporarily banned
    const PeerManager::AddrSet& get_AcessiblePeerAddrs() const;

	bool m_UpdatedFromPeers = false;
	bool m_PostStartSynced = false;

	bool GenerateRecoveryInfo(const char*);

	bool DecodeAndCheckHdrs(std::vector<Block::SystemState::Full>&, const proto::HdrPack&);

private:

	struct Processor
		:public NodeProcessor
	{
		// NodeProcessor
		void RequestData(const Block::SystemState::ID&, bool bBlock, const NodeDB::StateID& sidTrg) override;
		void OnPeerInsane(const PeerID&) override;
		void OnNewState() override;
		void OnRolledBack() override;
		void OnModified() override;
		bool EnumViewerKeys(IKeyWalker&) override;
		void OnUtxoEvent(const UtxoEvent::Value&, Height) override;
		void OnDummy(const Key::ID&, Height) override;
		void InitializeUtxosProgress(uint64_t done, uint64_t total) override;
		void Stop();

		struct TaskProcessor
			:public Task::Processor
		{
			virtual uint32_t get_Threads() override;
			virtual void Push(Task::Ptr&&) override;
			virtual uint32_t Flush(uint32_t nMaxTasks) override;
			virtual void ExecAll(Task&) override;

			std::mutex m_Mutex;

			std::deque<Task::Ptr> m_queTasks;
			uint32_t m_InProgress;
			uint32_t m_FlushTarget;
			bool m_Run;
			Task* m_pCtl;
			std::condition_variable m_NewTask;
			std::condition_variable m_Flushed;

			std::vector<std::thread> m_vThreads;

			typedef ECC::InnerProduct::BatchContextEx<4> MyBatch; // seems to be ok, for larger batches difference is marginal

			~TaskProcessor() { Stop(); }
			void Stop();
			void Thread(uint32_t);

			IMPLEMENT_GET_PARENT_OBJ(Node::Processor, m_TaskProcessor)

		private:
			void InitSafe();
			void FlushLocked(std::unique_lock<std::mutex>&, uint32_t nMaxTasks);

		} m_TaskProcessor;

		virtual Task::Processor& get_TaskProcessor() override { return m_TaskProcessor; }


		Block::ChainWorkProof m_Cwp; // cached
		bool BuildCwp();

		void GenerateProofStateStrict(Merkle::HardProof&, Height);

		bool m_bFlushPending = false;
		io::Timer::Ptr m_pFlushTimer;
		void OnFlushTimer();
		void FlushDB();

		bool m_bGoUpPending = false;
		io::Timer::Ptr m_pGoUpTimer;
		void TryGoUpAsync();
		void OnGoUpTimer();

		std::deque<PeerID> m_lstInsanePeers;
		io::AsyncEvent::Ptr m_pAsyncPeerInsane;
		void FlushInsanePeers();

		void DeleteOutdated();

		IMPLEMENT_GET_PARENT_OBJ(Node, m_Processor)
	} m_Processor;

	TxPool::Fluff m_TxPool;

	struct Peer;

	struct Task
		:public boost::intrusive::set_base_hook<>
		,public boost::intrusive::list_base_hook<>
	{
		typedef std::pair<Block::SystemState::ID, bool> Key;
		Key m_Key;

		bool m_bNeeded;
		uint32_t m_nCount;
		uint32_t m_TimeAssigned_ms;
		NodeDB::StateID m_sidTrg;
		Peer* m_pOwner;

		bool operator < (const Task& t) const { return (m_Key < t.m_Key); }
	};

	typedef boost::intrusive::list<Task> TaskList;
	typedef boost::intrusive::multiset<Task> TaskSet;

	uint32_t m_nTasksPackHdr = 0;
	uint32_t m_nTasksPackBody = 0;

	TaskList m_lstTasksUnassigned;
	TaskSet m_setTasks;

	void UpdateSyncStatus();
	void UpdateSyncStatusRaw();

	void TryAssignTask(Task&);
	bool TryAssignTask(Task&, Peer&);
	void DeleteUnassignedTask(Task&);

	void InitKeys();
	void InitIDs();
	void RefreshOwnedUtxos();
	void MaybeGenerateRecovery();

	struct Wanted
	{
		typedef ECC::Hash::Value KeyType;

		struct Item
			:public boost::intrusive::set_base_hook<>
			,public boost::intrusive::list_base_hook<>
		{
			KeyType m_Key;
			uint32_t m_Advertised_ms;

			bool operator < (const Item& n) const { return (m_Key < n.m_Key); }
		};

		typedef boost::intrusive::list<Item> List;
		typedef boost::intrusive::multiset<Item> Set;

		List m_lst;
		Set m_set;
		io::Timer::Ptr m_pTimer;

		void Delete(Item&);
		void DeleteInternal(Item&);
		void Clear();
		void SetTimer();
		void OnTimer();
		bool Add(const KeyType&);
		bool Delete(const KeyType&);

		~Wanted() { Clear(); }

		virtual uint32_t get_Timeout_ms() = 0;
		virtual void OnExpired(const KeyType&) = 0;
	};

	struct WantedTx :public Wanted {
		// Wanted
		virtual uint32_t get_Timeout_ms() override;
		virtual void OnExpired(const KeyType&) override;

		IMPLEMENT_GET_PARENT_OBJ(Node, m_Wtx)
	} m_Wtx;

	struct Dandelion
		:public TxPool::Stem
	{
		// TxPool::Stem
		virtual bool ValidateTxContext(const Transaction&, const HeightRange&) override;
		virtual void OnTimedOut(Element&) override;

		IMPLEMENT_GET_PARENT_OBJ(Node, m_Dandelion)
	} m_Dandelion;

	uint8_t OnTransactionStem(Transaction::Ptr&&, const Peer*);
	void OnTransactionAggregated(Dandelion::Element&);
	void PerformAggregation(Dandelion::Element&);
	void AddDummyInputs(Transaction&);
	bool AddDummyInputRaw(Transaction& tx, const Key::IDV&);
	bool AddDummyInputEx(Transaction& tx, const Key::IDV&);
	void AddDummyOutputs(Transaction&);
	Height SampleDummySpentHeight();
	bool OnTransactionFluff(Transaction::Ptr&&, const Peer*, Dandelion::Element*);

	uint8_t ValidateTx(Transaction::Context&, const Transaction&); // complete validation
	void LogTx(const Transaction&, uint8_t nStatus, const Transaction::KeyType&);
	void LogTxStem(const Transaction&, const char* szTxt);

	struct Bbs
	{
		struct WantedMsg :public Wanted {
			// Wanted
			virtual uint32_t get_Timeout_ms() override;
			virtual void OnExpired(const KeyType&) override;

			IMPLEMENT_GET_PARENT_OBJ(Bbs, m_W)
		} m_W;

		static void CalcMsgKey(NodeDB::WalkerBbs::Data&);
		uint32_t m_LastCleanup_ms = 0;
		void Cleanup();
		void MaybeCleanup();
		bool IsInLimits() const;

		struct Subscription
		{
			struct InBbs :public boost::intrusive::set_base_hook<> {
				BbsChannel m_Channel;
				bool operator < (const InBbs& x) const { return (m_Channel < x.m_Channel); }
				IMPLEMENT_GET_PARENT_OBJ(Subscription, m_Bbs)
			} m_Bbs;

			struct InPeer :public boost::intrusive::set_base_hook<> {
				BbsChannel m_Channel;
				bool operator < (const InPeer& x) const { return (m_Channel < x.m_Channel); }
				IMPLEMENT_GET_PARENT_OBJ(Subscription, m_Peer)
			} m_Peer;

			Peer* m_pPeer;
			uint64_t m_Cursor;

			typedef boost::intrusive::multiset<InBbs> BbsSet;
			typedef boost::intrusive::multiset<InPeer> PeerSet;
		};

		Subscription::BbsSet m_Subscribed;
		Timestamp m_HighestPosted_s = 0;

		NodeDB::BbsTotals m_Totals;

		IMPLEMENT_GET_PARENT_OBJ(Node, m_Bbs)
	} m_Bbs;

	struct PeerMan
		:public PeerManager
	{
		io::Timer::Ptr m_pTimerUpd;
		io::Timer::Ptr m_pTimerFlush;

		void Initialize();
		void OnFlush();

		struct PeerInfoPlus
			:public PeerInfo
		{
			struct AdjustedRatingLive
				:public boost::intrusive::set_base_hook<>
			{
				Peer* m_p;

				bool operator < (const AdjustedRatingLive& x) const { return (get_ParentObj().m_AdjustedRating < x.get_ParentObj().m_AdjustedRating); }

				IMPLEMENT_GET_PARENT_OBJ(PeerInfoPlus, m_Live)
			} m_Live;

			void Attach(Peer&);
			void DetachStrict();
		};

		// PeerManager
		virtual void ActivatePeer(PeerInfo&) override;
		virtual void DeactivatePeer(PeerInfo&) override;
		virtual PeerInfo* AllocPeer() override;
		virtual void DeletePeer(PeerInfo&) override;

		typedef boost::intrusive::multiset<PeerInfoPlus::AdjustedRatingLive> LiveSet;
		LiveSet m_LiveSet;

		~PeerMan() { Clear(); }

		IMPLEMENT_GET_PARENT_OBJ(Node, m_PeerMan)
	} m_PeerMan;

	struct Peer
		:public proto::NodeConnection
		,public boost::intrusive::list_base_hook<>
	{
		Node& m_This;

		PeerMan::PeerInfoPlus* m_pInfo;

		struct Flags
		{
			static const uint16_t Connected		= 0x001;
			static const uint16_t PiRcvd		= 0x002;
			static const uint16_t Owner			= 0x004;
			static const uint16_t Finalizing	= 0x080;
			static const uint16_t HasTreasury	= 0x100;
			static const uint16_t Chocking		= 0x200;
			static const uint16_t Viewer		= 0x400;
			static const uint16_t Accepted		= 0x800;
		};

		uint16_t m_Flags;
		uint16_t m_Port; // to connect to
		beam::io::Address m_RemoteAddr; // for logging only

		Block::SystemState::Full m_Tip;
		uint8_t m_LoginFlags;

		uint64_t m_CursorBbs;
		TxPool::Fluff::Element* m_pCursorTx;

		TaskList m_lstTasks;
		std::set<Task::Key> m_setRejected; // data that shouldn't be requested from this peer. Reset after reconnection or on receiving NewTip

		Bbs::Subscription::PeerSet m_Subscriptions;

		io::Timer::Ptr m_pTimerRequest;
		io::Timer::Ptr m_pTimerPeers;

		Peer(Node& n) :m_This(n) {}

		void TakeTasks();
		void ReleaseTasks();
		void ReleaseTask(Task&);
		void SetTimerWrtFirstTask();
		void Unsubscribe(Bbs::Subscription&);
		void Unsubscribe();
		void OnRequestTimeout();
		void OnResendPeers();
		void SendBbsMsg(const NodeDB::WalkerBbs::Data&);
		void DeleteSelf(bool bIsError, uint8_t nByeReason);
		void BroadcastTxs();
		void BroadcastBbs();
		void BroadcastBbs(Bbs::Subscription&);
		void OnChocking();
		void SetTxCursor(TxPool::Fluff::Element*);
		bool GetBlock(proto::BodyBuffers&, const NodeDB::StateID&, const proto::GetBodyPack&, bool bActive);

		bool IsChocking(size_t nExtra = 0);
		bool ShouldAssignTasks();
		bool ShouldFinalizeMining();
		Task& get_FirstTask();
		void OnFirstTaskDone();
		void OnFirstTaskDone(NodeProcessor::DataStatus::Enum);
		void ModifyRatingWrtData(size_t nSize);

		void SendTx(Transaction::Ptr& ptx, bool bFluff);

		// proto::NodeConnection
		virtual void OnConnectedSecure() override;
		virtual void OnDisconnect(const DisconnectReason&) override;
		virtual void GenerateSChannelNonce(ECC::Scalar::Native&) override; // Must be overridden to support SChannel
		// login
		virtual void SetupLogin(proto::Login&) override;
		virtual void OnLogin(proto::Login&&) override;
		virtual Height get_MinPeerFork() override;
		// messages
		virtual void OnMsg(proto::Authentication&&) override;
		virtual void OnMsg(proto::Bye&&) override;
		virtual void OnMsg(proto::Pong&&) override;
		virtual void OnMsg(proto::NewTip&&) override;
		virtual void OnMsg(proto::DataMissing&&) override;
		virtual void OnMsg(proto::GetHdr&&) override;
		virtual void OnMsg(proto::GetHdrPack&&) override;
		virtual void OnMsg(proto::HdrPack&&) override;
		virtual void OnMsg(proto::GetBody&&) override;
		virtual void OnMsg(proto::GetBodyPack&&) override;
		virtual void OnMsg(proto::Body&&) override;
		virtual void OnMsg(proto::BodyPack&&) override;
		virtual void OnMsg(proto::NewTransaction&&) override;
		virtual void OnMsg(proto::HaveTransaction&&) override;
		virtual void OnMsg(proto::GetTransaction&&) override;
		virtual void OnMsg(proto::GetCommonState&&) override;
		virtual void OnMsg(proto::GetProofState&&) override;
		virtual void OnMsg(proto::GetProofKernel&&) override;
		virtual void OnMsg(proto::GetProofKernel2&&) override;
		virtual void OnMsg(proto::GetProofUtxo&&) override;
		virtual void OnMsg(proto::GetProofChainWork&&) override;
		virtual void OnMsg(proto::PeerInfoSelf&&) override;
		virtual void OnMsg(proto::PeerInfo&&) override;
		virtual void OnMsg(proto::GetExternalAddr&&) override;
		virtual void OnMsg(proto::BbsMsg&&) override;
		virtual void OnMsg(proto::BbsHaveMsg&&) override;
		virtual void OnMsg(proto::BbsGetMsg&&) override;
		virtual void OnMsg(proto::BbsSubscribe&&) override;
		virtual void OnMsg(proto::BbsResetSync&&) override;
		virtual void OnMsg(proto::GetUtxoEvents&&) override;
		virtual void OnMsg(proto::BlockFinalization&&) override;
	};

	typedef boost::intrusive::list<Peer> PeerList;
	PeerList m_lstPeers;

	ECC::NoLeak<ECC::uintBig> m_NonceLast;
	const ECC::uintBig& NextNonce();
	void NextNonce(ECC::Scalar::Native&);

	uint32_t RandomUInt32(uint32_t threshold);

	ECC::Scalar::Native m_MyPrivateID;
	PeerID m_MyPublicID;

	Peer* AllocPeer(const beam::io::Address&);

	void RefreshCongestions();

	struct Server
		:public proto::NodeConnection::Server
	{
		// NodeConnection::Server
		virtual void OnAccepted(io::TcpStream::Ptr&&, int errorCode) override;

		IMPLEMENT_GET_PARENT_OBJ(Node, m_Server)
	} m_Server;

	struct Beacon
	{
		struct OutCtx;

		uv_udp_t* m_pUdp;
		OutCtx* m_pOut;
		std::vector<uint8_t> m_BufRcv;

		io::Timer::Ptr m_pTimer;
		void OnTimer();

		Beacon();
		~Beacon();

		void Start();
		uint16_t get_Port();

		static void OnClosed(uv_handle_t*);
		static void OnRcv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags);
		static void AllocBuf(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);

		IMPLEMENT_GET_PARENT_OBJ(Node, m_Beacon)
	} m_Beacon;

	struct PerThread
	{
		io::Reactor::Ptr m_pReactor;
		io::AsyncEvent::Ptr m_pEvt;
		std::thread m_Thread;
	};

	struct Miner
	{
		std::vector<PerThread> m_vThreads;
		io::AsyncEvent::Ptr m_pEvtMined;

		struct Task
			:public NodeProcessor::GeneratedBlock
		{
			typedef std::shared_ptr<Task> Ptr;

			// Task is mutable. But modifications are allowed only when holding the mutex.

			std::shared_ptr<volatile bool> m_pStop;

			ECC::Hash::Value m_hvNonceSeed; // immutable
		};

		bool IsEnabled() { return m_External.m_pSolver || !m_vThreads.empty(); }

		void Initialize(IExternalPOW* externalPOW=nullptr);

		void OnRefresh(uint32_t iIdx);
		void OnRefreshExternal();
		void OnMined();
		IExternalPOW::BlockFoundResult OnMinedExternal();
		void OnFinalizerChanged(Peer*);

		void HardAbortSafe();
		bool Restart();
		void StartMining(Task::Ptr&&);

		Peer* m_pFinalizer = NULL;
		Task::Ptr m_pTaskToFinalize;

		std::mutex m_Mutex;
		Task::Ptr m_pTask; // currently being-mined

		struct External
		{
			IExternalPOW* m_pSolver = nullptr;
			uint64_t m_jobID = 0;

			Task::Ptr m_ppTask[64]; // backlog of potentially being-mined currently
			Task::Ptr& get_At(uint64_t);

		} m_External;

		io::Timer::Ptr m_pTimer;
		bool m_bTimerPending = false;
		void OnTimer();
		void SetTimer(uint32_t timeout_ms, bool bHard);

		IMPLEMENT_GET_PARENT_OBJ(Node, m_Miner)
	} m_Miner;
};

} // namespace beam
