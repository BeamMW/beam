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
#include "../utility/dvector.h"
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

	UtxoTreeMapped m_Utxos;

	size_t m_nSizeUtxoComission;

	struct MultiblockContext;

	void RollbackTo(Height);
	Height PruneOld();
	Height RaiseFossil(Height);
	Height RaiseTxoLo(Height);
	Height RaiseTxoHi(Height);
	void Vacuum();
	void InitializeUtxos();
	void CommitUtxosAndDB();
	void RequestDataInternal(const Block::SystemState::ID&, uint64_t row, bool bBlock, const NodeDB::StateID& sidTrg);

	bool HandleTreasury(const Blob&);

	bool HandleBlock(const NodeDB::StateID&, MultiblockContext&);
	bool HandleValidatedTx(TxBase::IReader&&, Height, bool bFwd);
	bool HandleValidatedBlock(TxBase::IReader&&, const Block::BodyBase&, Height, bool bFwd);
	bool HandleBlockElement(const Input&, Height, bool bFwd);
	bool HandleBlockElement(const Output&, Height, bool bFwd);

	void RecognizeUtxos(TxBase::IReader&&, Height h);

	static uint64_t ProcessKrnMmr(Merkle::Mmr&, TxBase::IReader&&, const Merkle::Hash& idKrn, TxKernel::Ptr* ppRes);

	static const uint32_t s_TxoNakedMin = sizeof(ECC::Point); // minimal output size - commitment
	static const uint32_t s_TxoNakedMax = s_TxoNakedMin + 0x10; // In case the output has the Incubation period - extra size is needed (actually less than this).

	static void TxoToNaked(uint8_t* pBuf, Blob&);
	static bool TxoIsNaked(const Blob&);

	void ToInputWithMaturity(Input&, TxoID);

	TxoID get_TxosBefore(Height);
	void AdjustOffset(ECC::Scalar&, uint64_t rowid, bool bAdd);

	void InitCursor();
	bool InitUtxoMapping(const char*);
	static void OnCorrupted();
	void get_Definition(Merkle::Hash&, bool bForNextState);
	void get_Definition(Merkle::Hash&, const Merkle::Hash& hvHist);

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

		typedef boost::intrusive::list<TipCongestion> TipList;
		TipList m_lstTips;

		~CongestionCache() { Clear(); }

		void Clear();
		void Delete(TipCongestion*);
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

public:

	struct StartParams {
		bool m_ResetCursor = false;
		bool m_CheckIntegrityAndVacuum = false;
		bool m_ResetSelfID = false;
		bool m_EraseSelfID = false;
	};

	void Initialize(const char* szPath);
	void Initialize(const char* szPath, const StartParams&);

	virtual ~NodeProcessor();

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

	struct Cursor
	{
		// frequently used data
		NodeDB::StateID m_Sid;
		Block::SystemState::ID m_ID;
		Block::SystemState::Full m_Full;
		Merkle::Hash m_History;
		Merkle::Hash m_HistoryNext;
		Difficulty m_DifficultyNext;

	} m_Cursor;

	struct Extra
	{
		TxoID m_TxosTreasury;
		TxoID m_Txos; // total num of ever created TXOs, including treasury

		Height m_Fossil; // from here and down - no original blocks
		Height m_TxoLo;
		Height m_TxoHi;

	} m_Extra;

	struct SyncData
	{
		NodeDB::StateID m_Target; // can move fwd during sync
		Height m_h0;
		Height m_TxoLo;
		ECC::Point m_Sigma;

	} m_SyncData;

	bool IsFastSync() const { return m_SyncData.m_Target.m_Row != 0; }

	void SaveSyncData();
	void LogSyncData();

	bool ExtractBlockWithExtra(Block::Body&, const NodeDB::StateID&);

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
	UtxoTree& get_Utxos() { return m_Utxos; }

	Height get_ProofKernel(Merkle::Proof&, TxKernel::Ptr*, const Merkle::Hash& idKrn);

	void CommitDB();

	void EnumCongestions();
	const uint64_t* get_CachedRows(const NodeDB::StateID&, Height nCountExtra); // retval valid till next call to this func, or to EnumCongestions()
	void TryGoUp();
	void TryGoTo(NodeDB::StateID&);
	void OnFastSyncOver(MultiblockContext&, bool& bContextFail);

	// Lowest height to which it's possible to rollback.
	Height get_LowestReturnHeight() const;

	static bool IsRemoteTipNeeded(const Block::SystemState::Full& sTipRemote, const Block::SystemState::Full& sTipMy);

	virtual void RequestData(const Block::SystemState::ID&, bool bBlock, const NodeDB::StateID& sidTrg) {}
	virtual void OnPeerInsane(const PeerID&) {}
	virtual void OnNewState() {}
	virtual void OnRolledBack() {}
	virtual void OnModified() {}
	virtual void InitializeUtxosProgress(uint64_t done, uint64_t total) {}

	// parallel context-free execution
	struct Task
	{
		typedef std::unique_ptr<Task> Ptr;
		virtual void Exec() = 0;
        virtual ~Task() {};

		struct Processor
		{
			virtual uint32_t get_Threads();
			virtual void Push(Task::Ptr&&);
			virtual uint32_t Flush(uint32_t nMaxTasks);
			virtual void ExecAll(Task&);
		};
	};

	Task::Processor m_SyncProcessor;
	virtual Task::Processor& get_TaskProcessor() { return m_SyncProcessor; }

	bool ValidateAndSummarize(TxBase::Context&, const TxBase&, TxBase::IReader&&);
	bool VerifyBlock(const Block::BodyBase&, TxBase::IReader&&, const HeightRange&);

	struct IKeyWalker {
		virtual bool OnKey(Key::IPKdf&, Key::Index) = 0;
	};
	virtual bool EnumViewerKeys(IKeyWalker&) { return true; }

	bool Recover(Key::IDV&, const Output&, Height h);

	void RescanOwnedTxos();

	uint64_t FindActiveAtStrict(Height);

	bool ValidateTxContext(const Transaction&, const HeightRange&); // assuming context-free validation is already performed, but 
	bool ValidateTxWrtHeight(const Transaction&, const HeightRange&);
	bool ValidateInputs(const ECC::Point&, Input::Count = 1);

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

	struct ITxoRecover
		:public ITxoWalker
	{
		NodeProcessor& m_This;
		ITxoRecover(NodeProcessor& x) :m_This(x) {}

		virtual bool OnTxo(const NodeDB::WalkerTxo&, Height hCreate, Output&) override;
		virtual bool OnTxo(const NodeDB::WalkerTxo&, Height hCreate, Output&, const Key::IDV&) = 0;
	};

	struct ITxoWalker_UnspentNaked
		:public ITxoWalker
	{
		virtual bool OnTxo(const NodeDB::WalkerTxo&, Height hCreate) override;
	};

#pragma pack (push, 1)
	struct UtxoEvent
	{
		typedef ECC::Point Key;
		static_assert(sizeof(Key) == sizeof(ECC::uintBig) + 1, "");

		struct Value {
			ECC::Key::IDV::Packed m_Kidv;
			uintBigFor<Height>::Type m_Maturity;
			AssetID m_AssetID;
			uint8_t m_Added;
		};
	};
#pragma pack (pop)

	virtual void OnUtxoEvent(const UtxoEvent::Value&, Height) {}
	virtual void OnDummy(const Key::ID&, Height) {}

	static bool IsDummy(const Key::IDV&);

private:
	size_t GenerateNewBlockInternal(BlockContext&);
	void GenerateNewHdr(BlockContext&);
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
