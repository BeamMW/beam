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
#include <optional>

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

		uint32_t m_MaxConcurrentBlocksRequest = 200000; // high limit to allow parallel body downloads during fast sync
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
		int m_VerificationThreads = -1; // use all available cores for verification

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

			size_t m_MaxBodyPackSize = 1024 * 1024 * 50; // 50MB for faster sync
			uint32_t m_MaxBodyPackCount = 16000; // increased pack count

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
		IPbftHandler* get_PbftHandler() override;

		void Stop();

		struct MyExecutorMT
			:public ExecutorMT_R
		{
			void RunThread(uint32_t) override;

			~MyExecutorMT() { Stop(); }

			IMPLEMENT_GET_PARENT_OBJ(Node::Processor, m_ExecutorMT)
		} m_ExecutorMT;

		Executor& get_Executor() override { return m_ExecutorMT; }


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
		NodeDB::StateID m_sidTrgRequested;
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
		uint32_t get_Timeout_ms() override;
		void OnExpired(const KeyType&) override;

		IMPLEMENT_GET_PARENT_OBJ(Node, m_Wtx)
	} m_Wtx;

	struct Dandelion
		:public TxPool::Stem
	{
		// TxPool::Stem
		bool ValidateTxContext(const Transaction&, const HeightRange&, const AmountBig::Number&, Amount& feeReserve) override;
		void OnTimedOut(Element&) override;

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

		void OnSchedule() override;

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
			uint32_t get_Timeout_ms() override;
			void OnExpired(const KeyType&) override;

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
		void ActivatePeer(PeerInfo&) override;
		void DeactivatePeer(PeerInfo&) override;
		PeerInfo* AllocPeer() override;
		void DeletePeer(PeerInfo&) override;
		void ActivateMorePeers(uint32_t nTicks_ms) override;

		typedef boost::intrusive::multiset<PeerInfoPlus::AdjustedRatingLive> LiveSet;
		LiveSet m_LiveSet;

		~PeerMan() { Clear(); }

		IMPLEMENT_GET_PARENT_OBJ(Node, m_PeerMan)
	} m_PeerMan;

#define BeamNodeMsg_PbftHandleAll(macro) \
    macro(PbftRoundStart) \
    macro(PbftProposal) \
    macro(PbftVote) \
    macro(PbftPeerAssessment) \
    macro(PbftSigRequest) \
    macro(PbftSig) \


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

		NodeProcessor::Tip m_Tip;

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
		std::set<std::pair<Task::Key, uint64_t> > m_setRejected; // data that shouldn't be requested from this peer. Reset after reconnection or on receiving NewTip

		Bbs::Subscription::PeerSet m_Subscriptions;

		io::Timer::Ptr m_pTimerRequest;
		io::Timer::Ptr m_pTimerPeers;

		// Adaptive timeout tracking
		uint32_t m_AvgResponseTime_ms = 0;
		uint32_t m_nResponseSamples = 0;
		void UpdateResponseTime(uint32_t elapsed_ms);
		uint32_t GetAdaptiveTimeout(bool bBlock) const;

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
		void OnConnectedSecure() override;
		void OnDisconnect(const DisconnectReason&) override;
		void GenerateSChannelNonce(ECC::Scalar::Native&) override; // Must be overridden to support SChannel
		void OnTrafic(uint8_t msgCode, uint32_t msgSize, bool bOut) override;
		// login
		void SetupLogin(proto::Login&) override;
		void OnLogin(proto::Login&&, uint32_t nFlagsPrev) override;
		Height get_MinPeerFork() override;
		// messages
		void OnMsg(proto::Authentication&&) override;
		void OnMsg(proto::Bye&&) override;
		void OnMsg(proto::Pong&&) override;
		void OnMsg(proto::NewTip&&) override;
		void OnMsg(proto::DataMissing&&) override;
		void OnMsg(proto::GetHdr&&) override;
		void OnMsg(proto::GetHdrPack&&) override;
		void OnMsg(proto::HdrPack&&) override;
		void OnMsg(proto::EnumHdrs&&) override;
		void OnMsg(proto::GetBody&&) override;
		void OnMsg(proto::GetBodyPack&&) override;
		void OnMsg(proto::Body&&) override;
		void OnMsg(proto::BodyPack&&) override;
		void OnMsg(proto::NewTransaction&&) override;
		void OnMsg(proto::HaveTransaction&&) override;
		void OnMsg(proto::GetTransaction&&) override;
		void OnMsg(proto::GetCommonState&&) override;
		void OnMsg(proto::GetProofState&&) override;
		void OnMsg(proto::GetProofKernel&&) override;
		void OnMsg(proto::GetProofKernel2&&) override;
		void OnMsg(proto::GetProofKernel3&&) override;
		void OnMsg(proto::GetProofUtxo&&) override;
		void OnMsg(proto::GetProofShieldedOutp&&) override;
		void OnMsg(proto::GetProofShieldedInp&&) override;
		void OnMsg(proto::GetProofAsset&&) override;
		void OnMsg(proto::GetShieldedList&&) override;
		void OnMsg(proto::GetProofChainWork&&) override;
		void OnMsg(proto::PeerInfoSelf&&) override;
		void OnMsg(proto::PeerInfo&&) override;
		void OnMsg(proto::GetExternalAddr&&) override;
		void OnMsg(proto::BbsMsg&&) override;
		void OnMsg(proto::BbsHaveMsg&&) override;
		void OnMsg(proto::BbsGetMsg&&) override;
		void OnMsg(proto::BbsSubscribe&&) override;
		void OnMsg(proto::BbsResetSync&&) override;
		void OnMsg(proto::GetEvents&&) override;
		void OnMsg(proto::BlockFinalization&&) override;
		void OnMsg(proto::GetStateSummary&&) override;
		void OnMsg(proto::ContractVarsEnum&&) override;
		void OnMsg(proto::ContractLogsEnum&&) override;
		void OnMsg(proto::GetContractVar&&) override;
		void OnMsg(proto::GetContractLogProof&&) override;
		void OnMsg(proto::GetShieldedOutputsAt&&) override;
		void OnMsg(proto::SetDependentContext&&) override;
		void OnMsg(proto::GetAssetsListAt&&) override;
		void OnMsg(proto::PbftStamp&&) override;

#define THE_MACRO(msg) void OnMsg(proto::msg&&) override;
		BeamNodeMsg_PbftHandleAll(THE_MACRO)
#undef THE_MACRO
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
		void OnAccepted(io::TcpStream::Ptr&&, int errorCode) override;

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
		:public NodeProcessor::IPbftHandler
	{
		Validator();

		void Initialize();
		void OnNewState();
		void OnRolledBack();

#define THE_MACRO(msg) void OnMsg(proto::msg&&, const Peer&);
		BeamNodeMsg_PbftHandleAll(THE_MACRO)
#undef THE_MACRO

		void SendState(Peer&) const;

		// IPbftHandler
		const Block::Pbft::IValidatorSet& get_Validators() override;
		void OnContractVarChange(const Blob& key, const Blob& val, bool bTemporary) override;
		void OnContractStoreReset() override;
		bool OnContractInvoke(const ContractID&, uint32_t iMethod, const Blob& args, bool bTemporary) override;
		void TestBlock(const Block::SystemState::Full& s, const HeightHash& id, const Block::Body&, bool bTestOnly) override;
		bool AddReward(AmountBig::Number, bvm2::Storage::IBase&) override;

		uint64_t get_RefTime_ms() const;

		struct Stamp
		{
			HeightHash m_ID;
			proto::PbftStamp m_Msg;
		} m_Stamp;

		std::optional<ContractID> m_cid;

		void SaveStamp();
		bool ShouldSendStamp();

		struct Assessment
		{
			struct Settings;

			proto::PbftPeerAssessment m_Last;
			uint32_t m_Score = 0;
			Height m_hShouldSlashAt = 0;
		};

		struct ValidatorPlus
			:public intrusive::set_base_hook<Block::Pbft::Address>
		{
			typedef intrusive::multiset_autoclear<ValidatorPlus> Map;

			Merkle::Hash m_hvCommitted;

			struct PerRound
			{
				std::optional<ECC::Signature> m_pVote[2];
				std::optional<proto::PbftRoundStart> m_MsgRoundStart;

				struct Sig {
					ECC::Scalar::Native m_E; // challenge
					ECC::Scalar m_k; // response
				};

				std::optional<Sig> m_Sig;
			};

			PerRound m_pR[2]; // current and next

			uint64_t m_Weight;
			Assessment m_Assessment;
			uint8_t m_Status;
			uint8_t m_NumSlashed;
			Height m_hSuspend;
			bool m_Whitelisted;
			bool m_Deleted;

			uint64_t get_EffectiveWeight() const;
		};

		ValidatorPlus* m_pMe; // refreshed after each block

		struct ValidatorSet
			:public Block::Pbft::IValidatorSet
		{
			ValidatorPlus::Map m_mapValidators;

			bool EnumValidators(ITarget&) const override;

			ValidatorPlus* Find(const Block::Pbft::Address&) const;
			ValidatorPlus* FindActive(const Block::Pbft::Address&) const;
			ValidatorPlus& FindActiveStrict(const Block::Pbft::Address&) const;
			ValidatorPlus* SelectLeader(const Merkle::Hash& hvInp, uint32_t iRound) const;
			void get_Hash(Merkle::Hash&) const override;

			mutable std::optional<Merkle::Hash> m_hv;

		private:
			static uint64_t get_Random(ECC::Oracle&, uint64_t nBound);
		} m_ValidatorSet;

	private:

		struct RoundTimes
		{
			uint64_t m_iRound;
			uint64_t m_t0; // round start time
			uint64_t m_t1; // round end time
			uint64_t m_ts; // expected timestamp of a block proposed in this round

			void FromRound(const Rules&, const Block::SystemState::Full&);
			void FromT0(const Rules&, const Block::SystemState::Full&);

			//static void Test();

		private:
			void FromRoundProgressive(const Rules&, uint64_t tPrev_ms);
			void FromRoundInitial(const Rules&);
		};

		struct VoteKind {
			static const uint8_t PreVote = 0;
			static const uint8_t Commit = 1;
		};

		struct Power
		{
			uint64_t m_wVoted = 0;
			uint32_t m_nWhite = 0;

			void Add(const ValidatorPlus&);
			void Add(const Power&);
			bool IsMajorityReached(uint64_t wTotal) const;

			void Reset() { *this = Power(); }
		};

		struct PowerAndHash
			:public Power
		{
			Merkle::Hash m_hv;
		};

		struct Proposal
		{
			proto::PbftProposal m_Msg;
			Merkle::Hash m_hv;

			enum struct State {
				None,
				Received,
				Accepted
			} m_State;
		};

		struct RoundData
		{
			Proposal m_Proposal;

			const ValidatorPlus* m_pLeader;
			PowerAndHash m_pPwr[2];
			Power m_pwrNotCommitted;

			struct Multisig {
				proto::PbftSigRequest m_Msg;
				uint32_t m_SigsRemaining;
				ECC::Signature m_SigAggregate;
			};

			std::optional<Multisig> m_Multisig;

			void Reset();
			void SetHashes();
		};
		
		RoundData m_pR[2]; // current + next, some peers may send data a little too early, we'll accumulate them before processing
		ECC::Scalar::Native m_skNonce; // for current round only

		Proposal m_FutureCandidate;

		Height m_hAnchor;
		uint64_t m_iRound;
		uint64_t m_wTotal;

		enum struct State {
			None,
			Committed,
			QuorumReached,
		} m_State;

		void RefreshValidatorSet();
		void OnNewRound();
		void OnNewStateInternal();
		void GenerateProposal();
		void OnRoundStartReceived(uint32_t iR, ValidatorPlus&, const Peer*);
		void OnProposalReceived(uint32_t iR, const Peer*);
		void OnSigRequestReceived(uint32_t iR, const Peer*);
		void OnSigReceived(uint32_t iR, ValidatorPlus&, const Peer*);
		void CheckStateCurrent();
		void CheckState(uint32_t iR);
		bool SelectMultisigValidators(Bitmask<Block::Pbft::s_MaxValidators>& msk);
		bool ShouldAcceptProposal() const;
		bool CreateProposal();
		void CreateProposalMd(NodeProcessor::BlockContext&);
		void Sign(ECC::Signature&, const Merkle::Hash&);
		void MakeFullHdr(Block::SystemState::Full&, const Block::SystemState::Sequence::Element&) const;
		bool ShouldSendTo(const Peer&) const;
		void Vote(uint8_t iKind);
		void ShuffleRoundData(bool bConsequent);

		bool IsAssessmentRelevant(const proto::PbftPeerAssessment&) const;
		static void get_AssessmentMsg(Merkle::Hash&, const proto::PbftPeerAssessment&);
		void get_RoundStartMsg(Merkle::Hash&, const proto::PbftRoundStart&);
		void get_ProposalMsg(Merkle::Hash&, const RoundData&) const;
		void get_SigRequestMsg(Merkle::Hash&, const RoundData&, const proto::PbftSigRequest&) const;

		void FinalyzeRoundAssessment();
		uint32_t GetAssessment(const ValidatorPlus&);
		void UpdateAssessment(const ValidatorPlus&, uint8_t&);

		uint32_t get_PeerRound(const Peer&, uint32_t iRoundMsg);

		template <typename TMsg>
		void Broadcast(const TMsg&, const Peer* pSrc) const;

		io::Timer::Ptr m_pTimer;
		bool m_bTimerPending = false;
		void OnTimer();
		void SetTimer(uint64_t tNow_ms, uint64_t tTrg_ms);
		void KillTimer();

		IMPLEMENT_GET_PARENT_OBJ(Node, m_Validator)
	} m_Validator;

};

} // namespace beam
