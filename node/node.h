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
#include "core/shielded.h"
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
		virtual void OnRolledBack() {};
		virtual void InitializeUtxosProgress(uint64_t done, uint64_t total) {};
		virtual ILongAction* GetLongActionHandler() { return nullptr; }

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
		bool m_PeersPersistent = false; // keep connection to those peers, regardless to their rating

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
		uint32_t m_MaxDeferredTransactions = 100 * 1000;
		uint32_t m_MiningThreads = 0; // by default disabled

		bool m_LogEvents = false; // may be insecure. Off by default.
		bool m_LogTxStem = true;
		bool m_LogTxFluff = true;
		bool m_LogTraficUsage = false;

		bool m_PreferOnlineMining = true;

		// Number of verification threads for CPU-hungry cryptography. Currently used for block validation only.
		// 0: single threaded
		// negative: number of cores minus number of mining threads.
		int m_VerificationThreads = 0;

		struct RollbackLimit
		{
			uint32_t m_Max = 60; // artificial restriction on how much the node will rollback automatically
			uint32_t m_TimeoutSinceTip_s = 3600; // further rollback is possible after this timeout since the current tip
			// in either case it's no more than Rules::MaxRollback

		} m_RollbackLimit;

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
			uint32_t m_FakePowSolveTime_ms = 0;
			uint32_t m_TimeDrift_ms = 0;

		} m_TestMode;

		ByteBuffer m_Treasury; // needed only for the 1st run

		struct Dandelion
		{
			uint16_t m_FluffProbability = 0x1999; // normalized wrt 16 bit. Equals to 0.1
			uint32_t m_dhStemConfirm = 2; // if stem tx is not mined within this number of blocks (+1) - it's auto-fluffed

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
		IExternalPOW* m_pExternalPOW = nullptr;

	} m_Cfg; // must not be changed after initialization

	struct Keys
	{
		// There following Ptrs may point to the same object.

		Key::IKdf::Ptr m_pGeneric; // used for internal nonce generation. Auto-generated from system random if not specified
		Key::IPKdf::Ptr m_pOwner; // used for wallet authentication and UTXO tagging (this is the master view key)
		Key::IKdf::Ptr m_pMiner; // if not set - offline mining and decoy creation would be impossible

		Key::Index m_nMinerSubIndex = 0;

		struct Accounts
		{
			std::vector<Key::IPKdf::Ptr> m_vAdd; // new accounts to add

			struct Del
			{
				std::set<std::string> m_Eps; // endpoints to delete
				bool m_All = false; // delete all saved accounts
			} m_Del;

		} m_Accounts;

		void InitSingleKey(const ECC::uintBig& seed);
		void SetSingleKey(const Key::IKdf::Ptr&);

		struct Validator
		{
			ECC::Scalar::Native m_sk;
			Block::Pbft::Address m_Addr;
		} m_Validator;

	} m_Keys;

	~Node();

	void Initialize();

	NodeProcessor& get_Processor() { return m_Processor; } // for tests only!

	struct SyncStatus
	{
		static const uint32_t s_WeightHdr = 1;
		static const uint32_t s_WeightBlock = 8;

		// in units of Height, but different.
		uint64_t m_Done;
		uint64_t m_Total;

		bool operator == (const SyncStatus&) const;

		void ToRelative(uint64_t hDone0);

	} m_SyncStatus;

	uint32_t get_AcessiblePeerCount() const; // all the peers with known addresses. Including temporarily banned
	const PeerManager::AddrSet& get_AcessiblePeerAddrs() const;

	bool m_UpdatedFromPeers = false;
	bool m_PostStartSynced = false;

	bool GenerateRecoveryInfo(const char*);
	void PrintTxos();
	void PrintRollbackStats();

	void RefreshCongestions(); // call explicitly if manual rollback or forbidden state is modified

	bool DecodeAndCheckHdrs(std::vector<Block::SystemState::Full>&, const proto::HdrPack&);
	static bool DecodeAndCheckHdrsImpl(std::vector<Block::SystemState::Full>&, const proto::HdrPack&, ExecutorMT&);

	uint8_t OnTransaction(Transaction::Ptr&&, std::unique_ptr<Merkle::Hash>&&, const PeerID*, bool bFluff, std::ostream* pExtraInfo);

		// for step-by-step tests
	void GenerateFakeBlocks(uint32_t n);

	TxPool::Fluff m_TxPool;
	TxPool::Dependent m_TxDependent;
	std::map<Transaction::KeyType, uint8_t> m_TxReject; // spam

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
		void OnFastSyncSucceeded() override;
		void OnEvent(Height, const proto::Event::Base&) override;
		void OnDummy(const CoinID&, Height) override;
		void InitializeUtxosProgress(uint64_t done, uint64_t total) override;
		uint32_t get_MaxAutoRollback() override;
		void OnInvalidBlock(const Block::SystemState::Full&, const Block::Body&) override;
		void Stop();

		struct MyExecutorMT
			:public ExecutorMT_R
		{
			virtual void RunThread(uint32_t) override;

			~MyExecutorMT() { Stop(); }

			IMPLEMENT_GET_PARENT_OBJ(Node::Processor, m_ExecutorMT)
		} m_ExecutorMT;

		virtual Executor& get_Executor() override { return m_ExecutorMT; }


		Block::ChainWorkProof m_Cwp; // cached
		bool BuildCwp();

		void GenerateProofStateStrict(Merkle::HardProof&, Block::Number);
		void GenerateProofShielded(Merkle::Proof&, const uintBigFor<TxoID>::Type& mmrIdx);

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

		IMPLEMENT_GET_PARENT_OBJ(Node, m_Processor)
	} m_Processor;

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
		Block::Number m_n0; // those 2 are fast-sync params at the moment of task assignment
		Block::Number m_nTxoLo;
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
	void RefreshAccounts();
	struct AccountRefreshCtx;
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
		virtual bool ValidateTxContext(const Transaction&, const HeightRange&, const AmountBig::Number&, Amount& feeReserve) override;
		virtual void OnTimedOut(Element&) override;

		IMPLEMENT_GET_PARENT_OBJ(Node, m_Dandelion)
	} m_Dandelion;

	struct TxDeferred
		:public io::IdleEvt
	{
		struct Element
		{
			Transaction::Ptr m_pTx;
			std::unique_ptr<Merkle::Hash> m_pCtx;
			PeerID m_Sender;
			bool m_Fluff;
		};

		std::list<Element> m_lst;

		virtual void OnSchedule() override;

		IMPLEMENT_GET_PARENT_OBJ(Node, m_TxDeferred)
	} m_TxDeferred;

	void OnTransactionDeferred(Transaction::Ptr&&, std::unique_ptr<Merkle::Hash>&&, const PeerID*, bool bFluff);
	uint8_t OnTransactionStem(Transaction::Ptr&&, std::ostream* pExtraInfo);
	uint8_t OnTransactionFluff(Transaction::Ptr&&, std::ostream* pExtraInfo, const PeerID*, const TxPool::Stats*);
	void OnTransactionFluff(TxPool::Fluff::Element&, const PeerID*);
	uint8_t OnTransactionDependent(Transaction::Ptr&& pTx, const Merkle::Hash& hvCtx, const PeerID* pSender, bool bFluff, std::ostream* pExtraInfo);
	void OnTransactionAggregated(Transaction::Ptr&&, const TxPool::Stats&);
	void PerformAggregation(Dandelion::Element&);
	void AddDummyInputs(Transaction&, TxPool::Stats&);
	bool AddDummyInputRaw(Transaction& tx, const CoinID&);
	bool AddDummyInputEx(Transaction& tx, const CoinID&);
	void AddDummyOutputs(Transaction&, TxPool::Stats&);
	Height SampleDummySpentHeight();
	void DeleteOutdated();

	uint8_t ValidateTx(TxPool::Stats&, const Transaction&, const Transaction::KeyType& keyTx, std::ostream* pExtraInfo, bool& bAlreadyRejected); // complete validation
	uint8_t ValidateTx2(Transaction::Context&, const Transaction&, uint32_t& nBvmCharge, Amount& feeReserve, TxPool::Dependent::Element* pParent, std::ostream* pExtraInfo, Merkle::Hash* pNewCtx);
	static bool CalculateFeeReserve(const TxStats&, const HeightRange&, const AmountBig::Number&, uint32_t nBvmCharge, Amount& feeReserve);
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
		virtual void ActivateMorePeers(uint32_t nTicks_ms) override;

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
			static const uint16_t Probe			= 0x008;
			static const uint16_t SerifSent		= 0x010;
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

		struct DependentContext
		{
			std::unique_ptr<Merkle::Hash> m_pQuery;
			std::vector<Merkle::Hash> m_vSent;
			Block::Number m_nSent;
		} m_Dependent;

		uint64_t m_CursorBbs;
		TxPool::Fluff::Element::Send* m_pCursorTx;

		const NodeProcessor::Account* m_pAccount = nullptr;

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
		void MaybeSendSerif();
		void MaybeSendDependent();
		void OnChocking();
		void SetTxCursor(TxPool::Fluff::Element::Send*);
		bool GetBlock(proto::BodyBuffers&, const NodeDB::StateID&, const proto::GetBodyPack&, bool bActive);

		bool IsChocking(size_t nExtra = 0);
		bool ShouldAssignTasks();
		bool ShouldFinalizeMining();
		Task& get_FirstTask();
		bool ShouldAcceptBodyPack();
		void OnFirstTaskDone();
		void OnFirstTaskDone(NodeProcessor::DataStatus::Enum);
		void ModifyRatingWrtData(size_t nSize);
		void SendHdrs(NodeDB::StateID&, uint32_t nCount);
		void SendTx(Transaction::Ptr& ptx, bool bFluff, const Merkle::Hash* pCtx = nullptr);
		void OnNewTip2();
		void PbftSendStamp();

		struct ISelector {
			virtual bool IsValid(Peer&)= 0;
		};

		struct Selector_Stem :public ISelector {
			static bool IsValid_(Peer& p) {
				return !!(proto::LoginFlags::SpreadingTransactions & p.m_LoginFlags);
			}
			bool IsValid(Peer& p) override {
				return IsValid_(p);
			}
		};

		// proto::NodeConnection
		virtual void OnConnectedSecure() override;
		virtual void OnDisconnect(const DisconnectReason&) override;
		virtual void GenerateSChannelNonce(ECC::Scalar::Native&) override; // Must be overridden to support SChannel
		void OnTrafic(uint8_t msgCode, uint32_t msgSize, bool bOut) override;
		// login
		virtual void SetupLogin(proto::Login&) override;
		virtual void OnLogin(proto::Login&&, uint32_t nFlagsPrev) override;
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
		virtual void OnMsg(proto::EnumHdrs&&) override;
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
		virtual void OnMsg(proto::GetProofKernel3&&) override;
		virtual void OnMsg(proto::GetProofUtxo&&) override;
		virtual void OnMsg(proto::GetProofShieldedOutp&&) override;
		virtual void OnMsg(proto::GetProofShieldedInp&&) override;
		virtual void OnMsg(proto::GetProofAsset&&) override;
		virtual void OnMsg(proto::GetShieldedList&&) override;
		virtual void OnMsg(proto::GetProofChainWork&&) override;
		virtual void OnMsg(proto::PeerInfoSelf&&) override;
		virtual void OnMsg(proto::PeerInfo&&) override;
		virtual void OnMsg(proto::GetExternalAddr&&) override;
		virtual void OnMsg(proto::BbsMsg&&) override;
		virtual void OnMsg(proto::BbsHaveMsg&&) override;
		virtual void OnMsg(proto::BbsGetMsg&&) override;
		virtual void OnMsg(proto::BbsSubscribe&&) override;
		virtual void OnMsg(proto::BbsResetSync&&) override;
		virtual void OnMsg(proto::GetEvents&&) override;
		virtual void OnMsg(proto::BlockFinalization&&) override;
		virtual void OnMsg(proto::GetStateSummary&&) override;
		virtual void OnMsg(proto::ContractVarsEnum&&) override;
		virtual void OnMsg(proto::ContractLogsEnum&&) override;
		virtual void OnMsg(proto::GetContractVar&&) override;
		virtual void OnMsg(proto::GetContractLogProof&&) override;
		virtual void OnMsg(proto::GetShieldedOutputsAt&&) override;
		virtual void OnMsg(proto::SetDependentContext&&) override;
		virtual void OnMsg(proto::GetAssetsListAt&&) override;
		virtual void OnMsg(proto::PbftProposal&&) override;
		virtual void OnMsg(proto::PbftVote&&) override;
		virtual void OnMsg(proto::PbftStamp&&) override;
	};

	typedef boost::intrusive::list<Peer> PeerList;
	PeerList m_lstPeers;

	ECC::NoLeak<ECC::uintBig> m_NonceLast;
	const ECC::uintBig& NextNonce();
	void NextNonce(ECC::Scalar::Native&);

	uint32_t RandomUInt32(uint32_t threshold);


	Peer* SelectRandomPeer(Peer::ISelector&);

	ECC::Scalar::Native m_MyPrivateID;
	PeerID m_MyPublicID;

	Peer* AllocPeer(const beam::io::Address&);

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
		uint32_t m_FakeBlocksToGenerate = 0;

		void RunMinerThread(const io::Reactor::Ptr&, const Rules&);

		struct Task
			:public NodeProcessor::GeneratedBlock
		{
			typedef std::shared_ptr<Task> Ptr;

			// Task is mutable. But modifications are allowed only when holding the mutex.

			std::shared_ptr<volatile bool> m_pStop;

			ECC::Hash::Value m_hvNonceSeed; // immutable
		};

		bool IsEnabled() const;

		void Initialize();

		void SoftRestart();
		void OnRefresh(uint32_t iIdx);
		void OnRefreshExternal();
		void OnMined();
		IExternalPOW::BlockFoundResult OnMinedExternal();
		void OnFinalizerChanged(Peer*);
		bool IsShouldMine(const NodeProcessor::GeneratedBlock&);

		void HardAbortSafe();
		bool Restart();
		void StartMining(Task::Ptr&&);

		Peer* m_pFinalizer = NULL;
		Task::Ptr m_pTaskToFinalize;

		std::mutex m_Mutex;
		Task::Ptr m_pTask; // currently being-mined

		struct External
		{
			uint64_t m_jobID = 0;

			Task::Ptr m_ppTask[64]; // backlog of potentially being-mined currently
			Task::Ptr& get_At(uint64_t);

		} m_External;

		io::Timer::Ptr m_pTimer;
		bool m_bTimerPending = false;
		uint32_t m_LastRestart_ms;
		Amount m_FeesTrg = 0;
		void OnTimer();
		void SetTimer(uint32_t timeout_ms, bool bHard);

		IMPLEMENT_GET_PARENT_OBJ(Node, m_Miner)
	} m_Miner;

	struct Validator
	{
		const Block::Pbft::Validator* m_pMe; // refreshed after each block
		
		Validator();

		void OnNewState();
		void OnRolledBack();

		void OnMsg(proto::PbftProposal&&, const Peer&);
		void OnMsg(proto::PbftVote&&, const Peer&);
		void SendState(Peer&) const;

		uint64_t get_RefTime_ms() const;

		struct Stamp
		{
			HeightHash m_ID;
			ByteBuffer  m_vSer;
		} m_Stamp;

		void SaveStamp();
		bool ShouldSendStamp();

	private:

		struct VoteKind {
			static const uint8_t PreVote = 0;
			static const uint8_t Commit = 1;
			static const uint8_t NonCommitted = 2;
		};

		struct Power
		{
			uint64_t m_wVoted = 0;
			uint32_t m_nWhite = 0;

			void Add(const Block::Pbft::Validator&);
			bool IsMajorityReached(uint64_t wTotal) const;

			void Reset() { *this = Power(); }
		};

		struct SigsAndPower
		{
			std::map<Block::Pbft::Address, ECC::Signature> m_Sigs;
			Power m_Power;
			Merkle::Hash m_hv;

			void Add(const Block::Pbft::Validator&, const ECC::Signature&);

			void Reset()
			{
				m_Power.Reset();
				m_Sigs.clear();
			}
		};

		struct Proposal
		{
			proto::PbftProposal m_Msg;

			enum struct State {
				None,
				Received,
				Accepted
			} m_State;
		};

		struct RoundDataBase
		{
			Proposal m_Proposal;
			SigsAndPower m_spPreVoted;
			SigsAndPower m_spCommitted;

			void Reset();
			void SetHashes(const Block::SystemState::Full&);
			void get_LeaderMsg(Merkle::Hash&) const;
		};

		struct RoundData
			:public RoundDataBase
		{
			const Block::Pbft::Validator* m_pLeader;
			SigsAndPower m_spNotCommitted; // at the round start
			void Reset();
		};
		
		RoundData m_Current;
		RoundData m_Next; // some peers may send data a little too early, we'll accumulate them before processing

		RoundDataBase m_FutureCandidate;

		Merkle::Hash m_hvCommitted;

		Block::Number m_hAnchor;
		uint64_t m_iSlot0;
		uint64_t m_iRound;
		uint64_t m_wTotal;

		enum struct State {
			None,
			Committed,
			QuorumReached,
		} m_State;

		void OnNewRound();
		void GenerateProposal();
		void SignProposal();
		void OnProposalReceived(const Peer*);
		void CheckProposalCommit();
		void SendVotes(Peer*) const;
		void CheckState();
		bool ShouldAcceptProposal() const;
		void CheckQuorum(RoundData&);
		bool CreateProposal();
		void Sign(ECC::Signature&, const Merkle::Hash&);
		void MakeFullHdr(Block::SystemState::Full&, const Block::SystemState::Sequence::Element&) const;
		bool ShouldSendTo(const Peer&) const;

		RoundData* get_PeerRound(const Peer&, uint32_t iRoundMsg, bool& bCurrent);

		void Vote(uint8_t iKind, SigsAndPower&);
		void Vote_NotCommitted() { Vote(VoteKind::NonCommitted, m_Current.m_spNotCommitted); }
		void Vote_PreVote() { Vote(VoteKind::PreVote, m_Current.m_spPreVoted); }
		void Vote_Commit() { Vote(VoteKind::Commit, m_Current.m_spCommitted); }

		template <typename TMsg>
		void SendSigs(Peer*, TMsg&, const SigsAndPower&) const;

		template <typename TMsg>
		void Broadcast(const TMsg&, const Peer* pSrc) const;

		io::Timer::Ptr m_pTimer;
		bool m_bTimerPending = false;
		void OnTimer();
		void SetTimer(uint32_t timeout_ms);
		void SetTimerEx(uint64_t tNow_ms, uint64_t iSlotTrg);
		void KillTimer();

		IMPLEMENT_GET_PARENT_OBJ(Node, m_Validator)
	} m_Validator;

};

} // namespace beam
