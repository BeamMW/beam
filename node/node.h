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

namespace beam
{
	struct INodeObserver
	{
		virtual void OnSyncProgress(int done, int total) = 0;
		virtual void OnStateChanged() {}
	};

struct Node
{
	static const uint16_t s_PortDefault = 31744; // whatever

	struct Config
	{
		io::Address m_Listen;
		uint16_t m_BeaconPort = 0; // set to 0 if should use the same port for listen
		uint32_t m_BeaconPeriod_ms = 500;
		std::vector<io::Address> m_Connect;

		std::string m_sPathLocal;
		NodeProcessor::Horizon m_Horizon;

#if defined(BEAM_USE_GPU)
		bool m_UseGpu;
#endif

		struct Timeout {
			uint32_t m_GetState_ms	= 1000 * 5;
			uint32_t m_GetBlock_ms	= 1000 * 30;
			uint32_t m_GetTx_ms		= 1000 * 5;
			uint32_t m_GetBbsMsg_ms	= 1000 * 10;
			uint32_t m_MiningSoftRestart_ms = 100;
			uint32_t m_TopPeersUpd_ms = 1000 * 60 * 10; // once in 10 minutes
			uint32_t m_PeersUpdate_ms	= 1000; // reconsider every second
			uint32_t m_PeersDbFlush_ms = 1000 * 60; // 1 minute
			uint32_t m_BbsMessageTimeout_s	= 3600 * 24; // 1 day
			uint32_t m_BbsMessageMaxAhead_s	= 3600 * 2; // 2 hours
			uint32_t m_BbsCleanupPeriod_ms = 3600 * 1000; // 1 hour
		} m_Timeout;

		uint32_t m_MaxConcurrentBlocksRequest = 5;
		uint32_t m_BbsIdealChannelPopulation = 100;
		uint32_t m_MaxPoolTransactions = 100 * 1000;
		uint32_t m_MiningThreads = 0; // by default disabled

		// Number of verification threads for CPU-hungry cryptography. Currently used for block validation only.
		// 0: single threaded
		// negative: number of cores minus number of mining threads.
		int m_VerificationThreads = 0;

		struct HistoryCompression
		{
			std::string m_sPathOutput;
			std::string m_sPathTmp;

			uint32_t m_Naggling = 32;			// combine up to 32 blocks in memory, before involving file system
			uint32_t m_MaxBacklog = 7;

			uint32_t m_UploadPortion = 5 * 1024 * 1024; // set to 0 to disable upload

		} m_HistoryCompression;

		struct TestMode {
			// for testing only!
			uint32_t m_FakePowSolveTime_ms = 15 * 1000;

		} m_TestMode;

		std::vector<Block::Body> m_vTreasury;

		Block::SystemState::ID m_ControlState;

		struct Sync {
			// during sync phase we try to pick the best peer to sync from.
			// Our logic: decide when either examined enough peers, or timeout expires
			uint32_t m_SrcPeers = 5;
			uint32_t m_Timeout_ms = 10000;

			bool m_ForceResync = false;

		} m_Sync;

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

		Config()
		{
			m_ControlState.m_Height = Rules::HeightGenesis - 1; // disabled
		}

		INodeObserver* m_Observer = nullptr;

	} m_Cfg; // must not be changed after initialization

	struct Keys
	{
		// There following Ptrs may point to the same object.

		Key::IKdf::Ptr m_pGeneric; // used for internal nonce generation. Auto-generated from system random if not specified

		Key::IKdf::Ptr m_pMiner; // if not set - offline mining would be impossible
		Key::Index m_MinerIdx = 0;

		Key::IPKdf::Ptr m_pOwner; // used for wallet authentication

		typedef std::pair<Key::Index, Key::IPKdf::Ptr> Viewer;
		std::vector<Viewer> m_vMonitored;

		// legacy. To be removed!
		void InitSingleKey(const ECC::uintBig& seed);
		void SetSingleKey(const Key::IKdf::Ptr&);

	} m_Keys;

	~Node();
	void Initialize(IExternalPOW* externalPOW=nullptr);
	void ImportMacroblock(Height); // throws on err

	NodeProcessor& get_Processor() { return m_Processor; } // for tests only!

private:

	struct Processor
		:public NodeProcessor
	{
		// NodeProcessor
		void RequestData(const Block::SystemState::ID&, bool bBlock, const PeerID* pPreferredPeer) override;
		void OnPeerInsane(const PeerID&) override;
		void OnNewState() override;
		void OnRolledBack() override;
		bool VerifyBlock(const Block::BodyBase&, TxBase::IReader&&, const HeightRange&) override;
		bool ApproveState(const Block::SystemState::ID&) override;
		void AdjustFossilEnd(Height&) override;
		void OnStateData() override;
		void OnBlockData() override;
		void OnUpToDate() override;
		bool OpenMacroblock(Block::BodyBase::RW&, const NodeDB::StateID&) override;
		void OnModified() override;
		bool EnumViewerKeys(IKeyWalker&) override;

		void ReportProgress();
		void ReportNewState();

		struct Verifier
		{
			typedef ECC::InnerProduct::BatchContextEx<100> MyBatch; // seems to be ok, for larger batches difference is marginal

			const TxBase* m_pTx;
			TxBase::IReader* m_pR;
			TxBase::Context m_Context;

			bool m_bFail;
			uint32_t m_iTask;
			uint32_t m_Remaining;

			std::mutex m_Mutex;
			std::condition_variable m_TaskNew;
			std::condition_variable m_TaskFinished;

			std::vector<std::thread> m_vThreads;

			void Thread(uint32_t);

			IMPLEMENT_GET_PARENT_OBJ(Processor, m_Verifier)
		} m_Verifier;

		Block::ChainWorkProof m_Cwp; // cached
		bool BuildCwp();

		void GenerateProofStateStrict(Merkle::HardProof&, Height);

		int m_RequestedHeadersCount = 0;
		int m_RequestedBlocksCount = 0;
		int m_DownloadedHeaders = 0;
		int m_DownloadedBlocks = 0;

		bool m_bFlushPending = false;
		io::Timer::Ptr m_pFlushTimer;
		void OnFlushTimer();

		void FlushDB();

		std::deque<PeerID> m_lstInsanePeers;
		io::AsyncEvent::Ptr m_pAsyncPeerInsane;
		void FlushInsanePeers();

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

		bool m_bPack;
		bool m_bRelevant;
		Peer* m_pOwner;

		bool operator < (const Task& t) const { return (m_Key < t.m_Key); }
	};

	typedef boost::intrusive::list<Task> TaskList;
	typedef boost::intrusive::multiset<Task> TaskSet;

	uint32_t m_nTasksPackHdr = 0;
	uint32_t m_nTasksPackBody = 0;

	TaskList m_lstTasksUnassigned;
	TaskSet m_setTasks;

	struct FirstTimeSync
	{
		// there are 2 phases:
		//	1. Detection, pick the best peer to sync from
		//	2. Sync phase
		bool m_bDetecting;

		io::Timer::Ptr m_pTimer; // set during the 1st phase
		Difficulty::Raw m_Best;

		Block::SystemState::ID m_Trg;

		uint32_t m_RequestsPending = 0;
		uint8_t m_iData = 0;

		uint64_t m_SizeTotal;
		uint64_t m_SizeCompleted;
	};

	void OnSyncTimer();
	void SyncCycle();
	bool SyncCycle(Peer&);
	void SyncCycle(Peer&, const ByteBuffer&);

	std::unique_ptr<FirstTimeSync> m_pSync;

	void TryAssignTask(Task&, const PeerID*);
	bool TryAssignTask(Task&, Peer&);
	void DeleteUnassignedTask(Task&);

	void InitIDs();
	void InitMode();

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
		uint32_t m_Timeout_ms = 0;

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
		virtual bool ValidateTxContext(const Transaction&) override;
		virtual void OnTimedOut(Element&) override;

		IMPLEMENT_GET_PARENT_OBJ(Node, m_Dandelion)
	} m_Dandelion;

	bool OnTransactionStem(Transaction::Ptr&&, const Peer*);
	void OnTransactionAggregated(Dandelion::Element&);
	void PerformAggregation(Dandelion::Element&);
	void AddDummyInputs(Transaction&);
	void AddDummyOutputs(Transaction&);
	bool OnTransactionFluff(Transaction::Ptr&&, const Peer*, Dandelion::Element*);

	bool ValidateTx(Transaction::Context&, const Transaction&); // complete validation
	void LogTx(const Transaction&, bool bValid, const Transaction::KeyType&);

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
		BbsChannel m_RecommendedChannel = 0;
		void Cleanup();
		void FindRecommendedChannel();
		void MaybeCleanup();

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

			typedef boost::intrusive::multiset<InBbs> BbsSet;
			typedef boost::intrusive::multiset<InPeer> PeerSet;
		};

		Subscription::BbsSet m_Subscribed;

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
			Peer* m_pLive;
		};

		// PeerManager
		virtual void ActivatePeer(PeerInfo&) override;
		virtual void DeactivatePeer(PeerInfo&) override;
		virtual PeerInfo* AllocPeer() override;
		virtual void DeletePeer(PeerInfo&) override;

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
			static const uint8_t Connected		= 0x01;
			static const uint8_t PiRcvd			= 0x02;
			static const uint8_t Owner			= 0x04;
			static const uint8_t ProvenWorkReq	= 0x08;
			static const uint8_t ProvenWork		= 0x10;
			static const uint8_t SyncPending	= 0x20;
			static const uint8_t DontSync		= 0x40;
			static const uint8_t Finalizing		= 0x80;
		};

		uint8_t m_Flags;
		uint16_t m_Port; // to connect to
		beam::io::Address m_RemoteAddr; // for logging only

		Block::SystemState::Full m_Tip;
		uint8_t m_LoginFlags;

		TaskList m_lstTasks;
		std::set<Task::Key> m_setRejected; // data that shouldn't be requested from this peer. Reset after reconnection or on receiving NewTip

		Bbs::Subscription::PeerSet m_Subscriptions;

		io::Timer::Ptr m_pTimer;
		io::Timer::Ptr m_pTimerPeers;

		Peer(Node& n) :m_This(n) {}

		void TakeTasks();
		void ReleaseTasks();
		void ReleaseTask(Task&);
		void SetTimerWrtFirstTask();
		void Unsubscribe(Bbs::Subscription&);
		void Unsubscribe();
		void OnTimer();
		void SetTimer(uint32_t timeout_ms);
		void KillTimer();
		void OnResendPeers();
		void SendBbsMsg(const NodeDB::WalkerBbs::Data&);
		void DeleteSelf(bool bIsError, uint8_t nByeReason);

		bool ShouldAssignTasks();
		bool ShouldFinalizeMining();
		Task& get_FirstTask();
		void OnFirstTaskDone();
		void OnFirstTaskDone(NodeProcessor::DataStatus::Enum);

		void SendTx(Transaction::Ptr& ptx, bool bFluff);

		// proto::NodeConnection
		virtual void OnConnectedSecure() override;
		virtual void OnDisconnect(const DisconnectReason&) override;
		virtual void GenerateSChannelNonce(ECC::Scalar::Native&) override; // Must be overridden to support SChannel
		// messages
		virtual void OnMsg(proto::Authentication&&) override;
		virtual void OnMsg(proto::Login&&) override;
		virtual void OnMsg(proto::Bye&&) override;
		virtual void OnMsg(proto::Ping&&) override;
		virtual void OnMsg(proto::NewTip&&) override;
		virtual void OnMsg(proto::DataMissing&&) override;
		virtual void OnMsg(proto::GetHdr&&) override;
		virtual void OnMsg(proto::GetHdrPack&&) override;
		virtual void OnMsg(proto::Hdr&&) override;
		virtual void OnMsg(proto::HdrPack&&) override;
		virtual void OnMsg(proto::GetBody&&) override;
		virtual void OnMsg(proto::Body&&) override;
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
		virtual void OnMsg(proto::GetTime&&) override;
		virtual void OnMsg(proto::GetExternalAddr&&) override;
		virtual void OnMsg(proto::BbsMsg&&) override;
		virtual void OnMsg(proto::BbsHaveMsg&&) override;
		virtual void OnMsg(proto::BbsGetMsg&&) override;
		virtual void OnMsg(proto::BbsSubscribe&&) override;
		virtual void OnMsg(proto::BbsPickChannel&&) override;
		virtual void OnMsg(proto::MacroblockGet&&) override;
		virtual void OnMsg(proto::Macroblock&&) override;
		virtual void OnMsg(proto::ProofChainWork&&) override;
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

		bool IsEnabled() { return m_externalPOW || !m_vThreads.empty(); }

		void Initialize(IExternalPOW* externalPOW=nullptr);

		void OnRefresh(uint32_t iIdx);
		void OnRefreshExternal();
		void OnMined();
		void OnMinedExternal();
		void OnFinalizerChanged(Peer*);

		void HardAbortSafe();
		bool Restart();
		void StartMining(Task::Ptr&&);

		Peer* m_pFinalizer = NULL;
		Task::Ptr m_pTaskToFinalize;

		std::mutex m_Mutex;
		Task::Ptr m_pTask; // currently being-mined

		// external miner stuff
		IExternalPOW* m_externalPOW=nullptr;
		uint64_t m_jobID=0;
		Block::SystemState::Full m_savedState;

		io::Timer::Ptr m_pTimer;
		bool m_bTimerPending = false;
		void OnTimer();
		void SetTimer(uint32_t timeout_ms, bool bHard);

		IMPLEMENT_GET_PARENT_OBJ(Node, m_Miner)
	} m_Miner;

	struct Compressor
	{
		void Init();
		void OnRolledBack();
		void Cleanup();
		void Delete(const NodeDB::StateID&);
		void OnNewState();
		void FmtPath(std::string&, Height, const Height* pH0);
		void FmtPath(Block::BodyBase::RW&, Height, const Height* pH0);
		void StopCurrent();

		void OnNotify();
		void Proceed();
		bool ProceedInternal();
		bool SquashOnce(std::vector<HeightRange>&);
		bool SquashOnce(Block::BodyBase::RW&, Block::BodyBase::RW& rwSrc0, Block::BodyBase::RW& rwSrc1);
		uint64_t get_SizeTotal(Height);

		PerThread m_Link;
		std::mutex m_Mutex;
		std::condition_variable m_Cond;

		volatile bool m_bStop;
		bool m_bEnabled;
		bool m_bSuccess;

		// current data exchanged
		HeightRange m_hrNew; // requested range. If min is non-zero - should be merged with previously-generated
		HeightRange m_hrInplaceRequest;
		Merkle::Hash m_hvTag;

		IMPLEMENT_GET_PARENT_OBJ(Node, m_Compressor)
	} m_Compressor;
};

} // namespace beam
