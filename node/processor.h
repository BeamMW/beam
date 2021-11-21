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

#include "../core/radixtree.h"
#include "../core/proto.h"
#include "../core/treasury.h"
#include "../core/mapped_file.h"
#include "../utility/dvector.h"
#include "../utility/executor.h"
#include "../utility/containers.h"
#include "db.h"
#include "txpool.h"

namespace beam {

class NodeProcessor
{
	struct DB
		:public NodeDB
	{
		// NodeDB
		virtual void OnModified() override { get_ParentObj().OnModified(); }
		IMPLEMENT_GET_PARENT_OBJ(NodeProcessor, m_DB)
	} m_DB;

	NodeDB::Transaction m_DbTx;


	class Mapped
	{
		MappedFile m_Mapping;

		struct Type;

	protected:

		template <typename T>
		T* Allocate(uint32_t iBank)
		{
			return (T*) m_Mapping.Allocate(iBank, sizeof(T));
		}

	public:

		struct Utxo
			:public UtxoTree
		{
			virtual intptr_t get_Base() const override;

			virtual Leaf* CreateLeaf() override;
			virtual void DeleteEmptyLeaf(Leaf*) override;
			virtual Joint* CreateJoint() override;
			virtual void DeleteJoint(Joint*) override;

			virtual MyLeaf::IDQueue* CreateIDQueue() override;
			virtual void DeleteIDQueue(MyLeaf::IDQueue*) override;
			virtual MyLeaf::IDNode* CreateIDNode() override;
			virtual void DeleteIDNode(MyLeaf::IDNode*) override;

			friend class Mapped;

			virtual void OnDirty() override { get_ParentObj().OnDirty(); }

			void EnsureReserve();

			IMPLEMENT_GET_PARENT_OBJ(Mapped, m_Utxo)
		} m_Utxo;

		struct Contract
			:public RadixHashOnlyTree
		{
			virtual intptr_t get_Base() const override;

			virtual Leaf* CreateLeaf() override;
			virtual void DeleteLeaf(Leaf* p) override;
			virtual Joint* CreateJoint() override;
			virtual void DeleteJoint(Joint*) override;

			virtual void OnDirty() override { get_ParentObj().OnDirty(); }

			friend class Mapped;

			void EnsureReserve();

			void Toggle(const Blob& key, const Blob& data, bool bAdd);
			static bool IsStored(const Blob& key);

			IMPLEMENT_GET_PARENT_OBJ(Mapped, m_Contract)
		} m_Contract;

		void OnDirty();

		typedef Merkle::Hash Stamp;

		~Mapped() { Close(); }

		bool Open(const char* sz, const Stamp&);
		bool IsOpen() const { return m_Mapping.get_Base() != nullptr; }

		void Close();
		void FlushStrict(const Stamp&);

#pragma pack(push, 1)
		struct Hdr
		{
			MappedFile::Offset m_Dirty; // boolean, just aligned
			Stamp m_Stamp;
			MappedFile::Offset m_RootUtxo;
			MappedFile::Offset m_RootContract;
		};
#pragma pack(pop)

		Hdr& get_Hdr();
	};


	Mapped m_Mapped;

	size_t m_nSizeUtxoComission;

	struct MultiblockContext;
	struct MultiSigmaContext;
	struct MultiShieldedContext;
	struct MultiAssetContext;

	void RollbackTo(Height);
	Height PruneOld();
	Height RaiseFossil(Height);
	Height RaiseTxoLo(Height);
	Height RaiseTxoHi(Height);
	void Vacuum();
	void RebuildNonStd();
	void InitializeUtxos();
	bool TestDefinition();
	void TestDefinitionStrict();
	void CommitMappingAndDB();
	void RequestDataInternal(const Block::SystemState::ID&, uint64_t row, bool bBlock, const NodeDB::StateID& sidTrg);

	bool HandleTreasury(const Blob&);

	struct BlockInterpretCtx;
	struct ProcessorInfoParser;

	bool get_HdrAt(Block::SystemState::Full&);

	template <typename T>
	bool HandleElementVecFwd(const T& vec, BlockInterpretCtx&, size_t& n);
	template <typename T>
	void HandleElementVecBwd(const T& vec, BlockInterpretCtx&, size_t n);

	bool HandleBlock(const NodeDB::StateID&, const Block::SystemState::Full&, MultiblockContext&);
	bool HandleValidatedTx(const TxVectors::Full&, BlockInterpretCtx&);
	bool HandleValidatedBlock(const Block::Body&, BlockInterpretCtx&);
	bool HandleBlockElement(const Input&, BlockInterpretCtx&);
	bool HandleBlockElement(const Output&, BlockInterpretCtx&);
	bool HandleBlockElement(const TxKernel&, BlockInterpretCtx&);

	void InternalAssetAdd(Asset::Full&, bool bMmr);
	void InternalAssetDel(Asset::ID, bool bMmr);

	bool HandleAssetCreate(const PeerID&, const Asset::Metadata&, BlockInterpretCtx&, Asset::ID&, uint32_t nSubIdx = 0);
	bool HandleAssetEmit(const PeerID&, BlockInterpretCtx&, Asset::ID, AmountSigned, uint32_t nSubIdx = 0);
	bool HandleAssetDestroy(const PeerID&, BlockInterpretCtx&, Asset::ID, uint32_t nSubIdx = 0);

	bool HandleKernel(const TxKernel&, BlockInterpretCtx&);
	bool HandleKernelTypeAny(const TxKernel&, BlockInterpretCtx&);

#define THE_MACRO(id, name) bool HandleKernelType(const TxKernel##name&, BlockInterpretCtx&);
	BeamKernelsAll(THE_MACRO)
#undef THE_MACRO

	static uint64_t ProcessKrnMmr(Merkle::Mmr&, std::vector<TxKernel::Ptr>&, const Merkle::Hash& idKrn, TxKernel::Ptr* ppRes);

	struct KrnFlyMmr;

	static const uint32_t s_TxoNakedMin = sizeof(ECC::Point); // minimal output size - commitment
	static const uint32_t s_TxoNakedMax = s_TxoNakedMin + 0x10; // In case the output has the Incubation period - extra size is needed (actually less than this).

	static void TxoToNaked(uint8_t* pBuf, Blob&);
	static bool TxoIsNaked(const Blob&);

	void ToInputWithMaturity(Input&, Output&, bool bNake);

	TxoID get_TxosBefore(Height);
	TxoID FindHeightByTxoID(Height& h, TxoID id0); // returns the Txos at state end

	void ReadOffset(ECC::Scalar&, uint64_t rowid);
	void AdjustOffset(ECC::Scalar&, const ECC::Scalar& hvPrev, bool bAdd);

	void ReadKrns(uint64_t rowid, TxVectors::Eternal&);

	void InitCursor(bool bMovingUp);
	bool InitMapping(const char*, bool bForceReset);
	void InitializeMapped(const char*);

	typedef std::pair<int64_t, std::pair<int64_t, Difficulty::Raw> > THW; // Time-Height-Work. Time and Height are signed
	Difficulty get_NextDifficulty();
	Timestamp get_MovingMedian();
	void get_MovingMedianEx(Height, uint32_t nWindow, THW&);

	struct CongestionCache
	{
		struct TipCongestion
			:public boost::intrusive::list_base_hook<>
		{
			Height m_Height;
			bool m_bNeedHdrs;
			std::dvector<uint64_t> m_Rows;

			bool IsContained(const NodeDB::StateID&);
		};

		typedef intrusive::list_autoclear<TipCongestion> TipList;
		TipList m_lstTips;

		TipCongestion* Find(const NodeDB::StateID&);

	} m_CongestionCache;

	CongestionCache::TipCongestion* EnumCongestionsInternal();

	struct RecentStates
	{
		struct Entry
		{
			uint64_t m_RowID;
			Block::SystemState::Full m_State;
		};

		std::vector<Entry> m_vec;
		// cyclic buffer
		size_t m_i0 = 0;
		size_t m_Count = 0;

		Entry& get_FromTail(size_t) const;

		const Entry* Get(Height) const;
		void RollbackTo(Height);
		void Push(uint64_t rowID, const Block::SystemState::Full&);

	} m_RecentStates;

	void DeleteBlocksInRange(const NodeDB::StateID& sidTop, Height hStop);
	void DeleteBlock(uint64_t);

	void AdjustManualRollbackHeight(Height&);
	void ManualRollbackInternal(Height);

public:

	static void OnCorrupted();

	struct StartParams {
		bool m_CheckIntegrity = false;
		bool m_Vacuum = false;
		bool m_ResetSelfID = false;
		bool m_EraseSelfID = false;

		struct RichInfo {
			static const uint8_t Off = 1;
			static const uint8_t On = 2;
			static const uint8_t UpdShader = 4;
		};
		uint8_t m_RichInfoFlags = 0;
		Blob m_RichParser = Blob(nullptr, 0);
	};

	void Initialize(const char* szPath);
	void Initialize(const char* szPath, const StartParams&);

    static bool ExtractTreasury(const Blob&, Treasury::Data&);
	static void get_MappingPath(std::string&, const char*);

	NodeProcessor();
	virtual ~NodeProcessor();

	void ManualRollbackTo(Height);
	void ManualSelect(const Block::SystemState::ID&);

	struct Horizon {

		// branches behind this are pruned
		Height m_Branching;

		struct m_Schwarzschild {
			Height Lo; // spent behind this are completely erased
			Height Hi; // spent behind this are compacted
		};

		m_Schwarzschild m_Sync; // how deep to sync
		m_Schwarzschild m_Local; // how deep to keep

		void SetInfinite();
		void SetStdFastSync(); // Hi is minimum, Lo is 180 days

		void Normalize(); // make sure parameters are consistent w.r.t. each other and MaxRollback

		Horizon(); // by default all horizons are disabled, i.e. full archieve.

	} m_Horizon;

#pragma pack (push, 1)
	struct StateExtra
	{
		struct Base {
			ECC::Scalar m_TotalOffset;
		};

		struct Comms
		{
			Merkle::Hash m_hvCSA;
			Merkle::Hash m_hvLogs;
		};

		struct Full
			:public Base
			,public Comms
		{
		};
	};
#pragma pack (pop)

	struct Cursor
	{
		// frequently used data
		NodeDB::StateID m_Sid;
		Block::SystemState::ID m_ID;
		Block::SystemState::Full m_Full;
		Merkle::Hash m_History;
		Merkle::Hash m_HistoryNext;
		Difficulty m_DifficultyNext;
		StateExtra::Full m_StateExtra;
		Merkle::Hash m_hvKernels;
		bool m_bKernels;
	} m_Cursor;

	void EnsureCursorKernels();

	struct Extra
	{
		TxoID m_TxosTreasury;
		TxoID m_Txos; // total num of ever created TXOs, including treasury

		Height m_Fossil; // from here and down - no original blocks
		Height m_TxoLo;
		Height m_TxoHi;

		TxoID m_ShieldedOutputs;

	} m_Extra;

	struct SyncData
	{
		NodeDB::StateID m_Target; // can move fwd during sync
		Height m_h0;
		Height m_TxoLo;
		ECC::Point m_Sigma;

	} m_SyncData;

	struct ManualSelection
	{
		Block::SystemState::ID m_Sid; // set to MaxHeight if inactive
		bool m_Forbidden; // if not forbidden - this is the only valid state at the specified height

		bool Load();
		void Save() const;
		void Log() const;
		void Reset();
		void ResetAndSave();

		bool IsAllowed(Height, const Merkle::Hash&) const;
		bool IsAllowed(const Merkle::Hash&) const;

		IMPLEMENT_GET_PARENT_OBJ(NodeProcessor, m_ManualSelection)
	} m_ManualSelection;

	struct UnreachableLog
	{
		uint32_t m_Time_ms = 0;
		Merkle::Hash m_hvLast = Zero;

		void Log(const Block::SystemState::ID&);

	} m_UnreachableLog;

	bool IsFastSync() const { return m_SyncData.m_Target.m_Row != 0; }

	void SaveSyncData();
	void LogSyncData();

	struct ContractInvokeExtraInfo
	{
		ECC::uintBig m_Cid;

		FundsChangeMap m_FundsIO; // including nested
		std::vector<ECC::Point> m_vSigs; // excluding nested
		std::vector<ContractInvokeExtraInfo> m_vNested;
		std::string m_sParsed;

		void SetUnk(uint32_t iMethod, const Blob& args, const ECC::uintBig* pSid);

		template <typename Archive>
		void serialize(Archive& ar)
		{
			ar
				& m_Cid
				& m_FundsIO.m_Map
				& m_vSigs
				& m_vNested
				& m_sParsed;
		}
	};

	bool ExtractBlockWithExtra(Block::Body&, std::vector<Output::Ptr>& vOutsIn, const NodeDB::StateID&, std::vector<ContractInvokeExtraInfo>&);
	void get_ContractDescr(const ECC::uintBig& sid, const ECC::uintBig& cid, std::string&, bool bFullState);

	int get_AssetAt(Asset::Full&, Height); // Must set ID. Returns -1 if asset is destroyed, 0 if never existed.

	struct DataStatus {
		enum Enum {
			Accepted,
			Rejected, // duplicated or irrelevant
			Invalid,
			Unreachable // beyond lo horizon
		};
	};

	bool IsTreasuryHandled() const { return m_Extra.m_TxosTreasury > 0; }

	DataStatus::Enum OnState(const Block::SystemState::Full&, const PeerID&);
	DataStatus::Enum OnStateSilent(const Block::SystemState::Full&, const PeerID&, Block::SystemState::ID&, bool bAlreadyChecked);
	DataStatus::Enum OnBlock(const Block::SystemState::ID&, const Blob& bbP, const Blob& bbE, const PeerID&);
	DataStatus::Enum OnBlock(const NodeDB::StateID&, const Blob& bbP, const Blob& bbE, const PeerID&);
	DataStatus::Enum OnTreasury(const Blob&);

	// use only for data retrieval for peers
	NodeDB& get_DB() { return m_DB; }
	UtxoTree& get_Utxos() { return m_Mapped.m_Utxo; }
	RadixHashOnlyTree& get_Contracts() { return m_Mapped.m_Contract; }

	struct Evaluator
		:public Block::SystemState::Evaluator
	{
		NodeProcessor& m_Proc;
		Evaluator(NodeProcessor&);

		virtual bool get_History(Merkle::Hash&) override;
		virtual bool get_Utxos(Merkle::Hash&) override;
		virtual bool get_Kernels(Merkle::Hash&) override;
		virtual bool get_Logs(Merkle::Hash&) override;
		virtual bool get_Shielded(Merkle::Hash&) override;
		virtual bool get_Assets(Merkle::Hash&) override;
		virtual bool get_Contracts(Merkle::Hash&) override;
	};

	struct EvaluatorEx
		:public Evaluator
	{
		using Evaluator::Evaluator;

		void set_Kernels(const TxVectors::Eternal&);
		void set_Logs(const std::vector<Merkle::Hash>&);

		Merkle::Hash m_hvKernels;
		StateExtra::Comms m_Comms;
		virtual bool get_Kernels(Merkle::Hash&) override;
		virtual bool get_Logs(Merkle::Hash&) override;
		virtual bool get_CSA(Merkle::Hash&) override;
	};

	struct ProofBuilder
		:public Evaluator
	{
		Merkle::Proof& m_Proof;
		ProofBuilder(NodeProcessor& p, Merkle::Proof& proof)
			:Evaluator(p)
			,m_Proof(proof)
		{
		}

	protected:
		virtual void OnProof(Merkle::Hash&, bool);
	};

	struct ProofBuilderHard
		:public Evaluator
	{
		Merkle::HardProof& m_Proof;
		ProofBuilderHard(NodeProcessor& p, Merkle::HardProof& proof)
			:Evaluator(p)
			,m_Proof(proof)
		{
		}

	protected:
		virtual void OnProof(Merkle::Hash&, bool);
	};

	struct ProofBuilder_PrevState;

	Height get_ProofKernel(Merkle::Proof&, TxKernel::Ptr*, const Merkle::Hash& idKrn);
	bool get_ProofContractLog(Merkle::Proof&, const HeightPos&);

	void CommitDB();

	void EnumCongestions();
	const uint64_t* get_CachedRows(const NodeDB::StateID&, Height nCountExtra); // retval valid till next call to this func, or to EnumCongestions()
	void TryGoUp();
	void TryGoTo(NodeDB::StateID&);
	void OnFastSyncOver(MultiblockContext&, bool& bContextFail);

	// Lowest height to which it's possible to rollback.
	Height get_LowestReturnHeight();
	Height get_LowestManualReturnHeight();

	static bool IsRemoteTipNeeded(const Block::SystemState::Full& sTipRemote, const Block::SystemState::Full& sTipMy);

	virtual void RequestData(const Block::SystemState::ID&, bool bBlock, const NodeDB::StateID& sidTrg) {}
	virtual void OnPeerInsane(const PeerID&) {}
	virtual void OnNewState() {}
	virtual void OnRolledBack() {}
	virtual void OnModified() {}
	virtual void InitializeUtxosProgress(uint64_t done, uint64_t total) {}
	virtual void OnFastSyncSucceeded() {}
	virtual Height get_MaxAutoRollback();

	struct MyExecutor
		:public Executor
	{
		struct MyContext
			:public Context
		{
			ECC::InnerProduct::BatchContextEx<4> m_BatchCtx; // seems to be ok, for larger batches difference is marginal
		};

		MyContext m_Ctx;

		virtual uint32_t get_Threads();
		virtual void Push(TaskAsync::Ptr&&);
		virtual uint32_t Flush(uint32_t nMaxTasks);
		virtual void ExecAll(TaskSync&);
	};

	std::unique_ptr<MyExecutor> m_pExecSync;

	virtual Executor& get_Executor();

	bool ValidateAndSummarize(TxBase::Context&, const TxBase&, TxBase::IReader&&);

	struct ViewerKeys
	{
		Key::IPKdf* m_pMw;
		ShieldedTxo::Viewer* m_pSh;
		Key::Index m_nSh;
		
		bool IsEmpty() const;
	};

	virtual void get_ViewerKeys(ViewerKeys&);

	void RescanOwnedTxos();

	uint64_t FindActiveAtStrict(Height);
	Height FindVisibleKernel(const Merkle::Hash&, const BlockInterpretCtx&);

	uint8_t ValidateTxContextEx(const Transaction&, const HeightRange&, bool bShieldedTested, uint32_t& nBvmCharge, TxPool::Dependent::Element* pParent, std::ostream* pExtraInfo); // assuming context-free validation is already performed, but 
	bool ValidateInputs(const ECC::Point&, Input::Count = 1);
	bool ValidateUniqueNoDup(BlockInterpretCtx&, const Blob& key, const Blob* pVal);
	void ManageKrnID(BlockInterpretCtx&, const TxKernel&);

	bool IsShieldedInPool(const Transaction&);
	bool IsShieldedInPool(const TxKernelShieldedInput&);

	struct GeneratedBlock
	{
		Block::SystemState::Full m_Hdr;
		ByteBuffer m_BodyP;
		ByteBuffer m_BodyE;
		Amount m_Fees;
		Block::Body m_Block; // in/out
	};


	struct BlockContext
		:public GeneratedBlock
	{
		TxPool::Fluff& m_TxPool;

		Key::Index m_SubIdx;
		Key::IKdf& m_Coin;
		Key::IPKdf& m_Tag;

		enum Mode {
			Assemble,
			Finalize,
			SinglePass
		};

		Mode m_Mode = Mode::SinglePass;

		BlockContext(TxPool::Fluff& txp, Key::Index, Key::IKdf& coin, Key::IPKdf& tag);
	};

	bool GenerateNewBlock(BlockContext&);

	bool GetBlock(const NodeDB::StateID&, ByteBuffer* pEthernal, ByteBuffer* pPerishable, Height h0, Height hLo1, Height hHi1, bool bActive);

	struct ITxoWalker
	{
		// override at least one of those
		virtual bool OnTxo(const NodeDB::WalkerTxo&, Height hCreate);
		virtual bool OnTxo(const NodeDB::WalkerTxo&, Height hCreate, Output&);
	};

	bool EnumTxos(ITxoWalker&);
	bool EnumTxos(ITxoWalker&, const HeightRange&);

	struct ITxoWalker_Unspent
		:public ITxoWalker
	{
		virtual bool OnTxo(const NodeDB::WalkerTxo&, Height hCreate) override;
	};

	struct ITxoRecover
		:public ITxoWalker
	{
		Key::IPKdf& m_Key;
		ITxoRecover(Key::IPKdf& key) :m_Key(key) {}

		virtual bool OnTxo(const NodeDB::WalkerTxo&, Height hCreate) override;
		virtual bool OnTxo(const NodeDB::WalkerTxo&, Height hCreate, Output&) override;
		virtual bool OnTxo(const NodeDB::WalkerTxo&, Height hCreate, Output&, const CoinID&, const Output::User&) = 0;
	};

	struct ITxoWalker_UnspentNaked
		:public ITxoWalker
	{
		virtual bool OnTxo(const NodeDB::WalkerTxo&, Height hCreate) override;
	};

	struct IKrnWalker
		:public TxKernel::IWalker
	{
		virtual bool ProcessHeight(const std::vector<TxKernel::Ptr>& v) { return Process(v); }
		Height m_Height;
	};

	bool EnumKernels(IKrnWalker&, const HeightRange&);

	struct KrnWalkerShielded
		:public IKrnWalker
	{
		virtual bool OnKrn(const TxKernel& krn) override;
		virtual bool OnKrnEx(const TxKernelShieldedInput&) { return true; }
		virtual bool OnKrnEx(const TxKernelShieldedOutput&) { return true; }
	};

	struct Recognizer;
	struct KrnWalkerRecognize
		:public IKrnWalker
	{
		Recognizer& m_Proc;
		KrnWalkerRecognize(Recognizer& p) :m_Proc(p) {}

		virtual bool OnKrn(const TxKernel& krn) override;
	};

#pragma pack (push, 1)
	struct EventKey
	{
		// make sure we always distinguish different events by their keys
		typedef ECC::Point Utxo;
		typedef ECC::Point Shielded;

		typedef PeerID AssetCtl;

		// Utxo and Shielded use the same key type, hence the following flag (OR-ed with Y coordinate) makes the difference
		static const uint8_t s_FlagShielded = 2;

		typedef NodeDB::EventIndexType IndexType;

		static const IndexType s_IdxInput = 0;
		static const IndexType s_IdxOutput = 1;
		static const IndexType s_IdxKernel = 2;
	};

	struct ShieldedBase
	{
		uintBigFor<TxoID>::Type m_MmrIndex;
		uintBigFor<Height>::Type m_Height;
	};

	struct ShieldedOutpPacked
		:public ShieldedBase
	{
		ECC::Point m_Commitment;
		uintBigFor<TxoID>::Type m_TxoID;
	};

	struct ShieldedInpPacked
		:public ShieldedBase
	{
	};

	struct AssetDataPacked {
		AmountBig::Type m_Amount;
		uintBigFor<Height>::Type m_LockHeight;

		void set_Strict(const Blob&);
	};

	struct AssetCreateInfoPacked {
		PeerID m_Owner;
		// followed by metadata
	};

#pragma pack (pop)

	struct Recognizer
	{
		struct IEventHandler
		{
			// returns true to stop enumeration
			virtual bool OnEvent(Height, const Blob& body) = 0;
		};

		struct IHandler
		{
			virtual void get_ViewerKeys(NodeProcessor::ViewerKeys& vk) {}
			virtual void OnDummy(const CoinID&, Height) {}
			virtual void OnEvent(Height, const proto::Event::Base&) {}
			virtual void AssetEvtsGetStrict(NodeDB::AssetEvt& event, Height h, uint32_t nKrnIdx) {}
			virtual void InsertEvent(Height h, const Blob& b, const Blob& key) {}
			virtual bool FindEvents(const Blob& key, IEventHandler&) { return false; }
		};
		Recognizer(IHandler& h, Extra& extra);

		void Recognize(const TxVectors::Full& block, Height height, uint32_t shieldedOuts, bool validateShieldedOuts = true);

		void Recognize(const Input&, Height);
		void Recognize(const Output&, Height, Key::IPKdf&);

#define THE_MACRO(id, name) void Recognize(const TxKernel##name&, Height, uint32_t);
		BeamKernelsAll(THE_MACRO)
#undef THE_MACRO

		template <typename TKey, typename TEvt>
		bool FindEvent(const TKey&, TEvt&);

		template <typename TEvt, typename TKey>
		void AddEvent(Height, EventKey::IndexType nIdx, const TEvt&, const TKey&);

		template <typename TEvt>
		void AddEvent(Height, EventKey::IndexType nIdx, const TEvt&);
	private:
		template <typename TEvt>
		void AddEventInternal(Height, EventKey::IndexType nIdx, const TEvt&, const Blob& key);

		IHandler& m_Handler;
		Extra& m_Extra;
	};

	struct MyRecognizer;

	virtual void OnEvent(Height, const proto::Event::Base&) {}
	virtual void OnDummy(const CoinID&, Height) {}

	static bool IsContractVarStoredInMmr(const Blob& key) {
		return Mapped::Contract::IsStored(key);
	}

	struct Mmr
	{
		Mmr(NodeDB&);
		NodeDB::StatesMmr m_States;
		NodeDB::StreamMmr m_Shielded;
		NodeDB::StreamMmr m_Assets;

	} m_Mmr;

	TxoID get_ShieldedInputs() const {
		return m_Mmr.m_Shielded.m_Count - m_Extra.m_ShieldedOutputs;
	}

	struct ValidatedCache
	{
		struct Entry
		{
			struct Key
				:public boost::intrusive::set_base_hook<>
			{
				typedef ECC::Hash::Value Type;
				Type m_Value;
				bool operator < (const Key& x) const { return m_Value < x.m_Value; }
				IMPLEMENT_GET_PARENT_OBJ(Entry, m_Key)
			} m_Key;

			struct Mru
				:public boost::intrusive::list_base_hook<>
			{
				IMPLEMENT_GET_PARENT_OBJ(Entry, m_Mru)
			} m_Mru;

			struct ShLo
				:public boost::intrusive::set_base_hook<>
			{
				typedef TxoID Type;
				Type m_End;
				bool operator < (const ShLo& x) const { return m_End < x.m_End; }
				IMPLEMENT_GET_PARENT_OBJ(Entry, m_ShLo)
			} m_ShLo;
		};

		typedef boost::intrusive::multiset<Entry::Key> KeySet;
		typedef boost::intrusive::multiset<Entry::ShLo> ShLoSet;
		typedef boost::intrusive::list<Entry::Mru> MruList;

		KeySet m_Keys;
		ShLoSet m_ShLo;
		MruList m_Mru;

		~ValidatedCache() {
			ShrinkTo(0);
		}

		void Delete(Entry&);
		void MoveToFront(Entry&);

		void ShrinkTo(uint32_t);
		void OnShLo(const Entry::ShLo::Type& nShLo);

		bool Find(const Entry::Key::Type&); // modifies MRU if found
		void Insert(const Entry::Key::Type&, const Entry::ShLo::Type& nShLo);

		void MoveInto(ValidatedCache& dst);

	protected:
		void InsertRaw(Entry&);
		void RemoveRaw(Entry&);

	} m_ValCache;

private:
	size_t GenerateNewBlockInternal(BlockContext&, BlockInterpretCtx&);
	void GenerateNewHdr(BlockContext&, BlockInterpretCtx&);
	DataStatus::Enum OnStateInternal(const Block::SystemState::Full&, Block::SystemState::ID&, bool bAlreadyChecked);
	bool GetBlockInternal(const NodeDB::StateID&, ByteBuffer* pEthernal, ByteBuffer* pPerishable, Height h0, Height hLo1, Height hHi1, bool bActive, Block::Body*);
};

struct LogSid
{
	NodeDB& m_DB;
	const NodeDB::StateID& m_Sid;

	LogSid(NodeDB& db, const NodeDB::StateID& sid)
		:m_DB(db)
		, m_Sid(sid)
	{}
};

std::ostream& operator << (std::ostream& s, const LogSid&);


} // namespace beam
