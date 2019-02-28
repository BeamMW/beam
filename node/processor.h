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

	UtxoTree m_Utxos;

	size_t m_nSizeUtxoComission;

	void TryGoUp();

	bool GoForward(uint64_t);
	void Rollback();
	Height PruneOld();
	void InitializeFromBlocks();
	void InitializeUtxos(TxoID nTreasury);
	void RequestDataInternal(const Block::SystemState::ID&, uint64_t row, bool bBlock, Height hTarget);

	struct RollbackData;

	bool HandleTreasury(const Blob&);

	bool HandleBlock(const NodeDB::StateID&, bool bFwd);
	bool HandleValidatedTx(TxBase::IReader&&, Height, bool bFwd, const Height* = NULL);
	bool HandleValidatedBlock(TxBase::IReader&&, const Block::BodyBase&, Height, bool bFwd, const Height* = NULL);
	bool HandleBlockElement(const Input&, Height, const Height*, bool bFwd);
	bool HandleBlockElement(const Output&, Height, const Height*, bool bFwd);

	bool ImportMacroBlockInternal(Block::BodyBase::IMacroReader&);
	void RecognizeUtxos(TxBase::IReader&&, Height hMax);

	static void SquashOnce(std::vector<Block::Body>&);
	static uint64_t ProcessKrnMmr(Merkle::Mmr&, TxBase::IReader&&, Height, const Merkle::Hash& idKrn, TxKernel::Ptr* ppRes);

	static const uint32_t s_TxoNakedMin = sizeof(ECC::Point); // minimal output size - commitment
	static const uint32_t s_TxoNakedMax = s_TxoNakedMin + 0x10; // In case the output has the Incubation period - extra size is needed (actually less than this).

	static void TxoToNaked(uint8_t* pBuf, Blob&);

	void InitCursor();
	static void OnCorrupted();
	void get_Definition(Merkle::Hash&, bool bForNextState);
	void get_Definition(Merkle::Hash&, const Merkle::Hash& hvHist);

	typedef std::pair<int64_t, std::pair<int64_t, Difficulty::Raw> > THW; // Time-Height-Work. Time and Height are signed
	Difficulty get_NextDifficulty();
	Timestamp get_MovingMedian();
	void get_MovingMedianEx(uint64_t rowLast, uint32_t nWindow, THW&);

	struct UtxoSig;
	struct UnspentWalker;

	struct IBlockWalker
	{
		virtual bool OnBlock(const Block::BodyBase&, TxBase::IReader&&, uint64_t rowid, Height, const Height* pHMax) = 0;
	};

	struct IUtxoWalker
		:public IBlockWalker
	{
		NodeProcessor& m_This;
		IUtxoWalker(NodeProcessor& x) :m_This(x) {}

		Block::SystemState::Full m_Hdr;

		virtual bool OnBlock(const Block::BodyBase&, TxBase::IReader&&, uint64_t rowid, Height, const Height* pHMax) override;

		virtual bool OnInput(const Input&) = 0;
		virtual bool OnOutput(const Output&) = 0;
	};

	bool EnumBlocks(IBlockWalker&);
	Height OpenLatestMacroblock(Block::Body::RW&);

	struct CongestionCache
	{
		struct TipCongestion
			:public boost::intrusive::list_base_hook<>
		{
			Height m_Height;
			std::dvector<uint64_t> m_Rows;
		};

		typedef boost::intrusive::list<TipCongestion> TipList;
		TipList m_lstTips;

		~CongestionCache() { Clear(); }

		void Clear();
		void Delete(TipCongestion*);
		TipCongestion* Find(const NodeDB::StateID&);

	} m_CongestionCache;

public:

	void Initialize(const char* szPath, bool bResetCursor = false);
	virtual ~NodeProcessor();

	struct Horizon {

		Height m_Branching; // branches behind this are pruned
		Height m_SchwarzschildLo; // spent behind this are completely erased
		Height m_SchwarzschildHi; // spent behind this are compacted

		Horizon(); // by default both are disabled.

	} m_Horizon;

	void OnHorizonChanged();

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

	typedef NodeDB::StateExtra StateExtra;

	struct Extra
	{
		bool m_TreasuryHandled;
		TxoID m_Txos; // total num of ever created TXOs, including treasury

		Height m_LoHorizon; // lowest accessible height
		Height m_Fossil; // from here and down - no original blocks
		Height m_TxoLo;
		Height m_TxoHi;

	} m_Extra;

	// Export compressed history elements. Suitable only for "small" ranges, otherwise may be both time & memory consumng.
	void ExtractBlockWithExtra(Block::Body&, const NodeDB::StateID&);
	void ExportMacroBlock(Block::BodyBase::IMacroWriter&, const HeightRange&);
	void ExportHdrRange(const HeightRange&, Block::SystemState::Sequence::Prefix&, std::vector<Block::SystemState::Sequence::Element>&);
	bool ImportMacroBlock(Block::BodyBase::IMacroReader&);

	struct DataStatus {
		enum Enum {
			Accepted,
			Rejected, // duplicated or irrelevant
			Invalid,
			Unreachable // beyond lo horizon
		};
	};

	DataStatus::Enum OnState(const Block::SystemState::Full&, const PeerID&);
	DataStatus::Enum OnBlock(const Block::SystemState::ID&, const Blob& bbP, const Blob& bbE, const PeerID&);
	DataStatus::Enum OnTreasury(const Blob&);

	// use only for data retrieval for peers
	NodeDB& get_DB() { return m_DB; }
	UtxoTree& get_Utxos() { return m_Utxos; }
	static void ReadBody(Block::Body&, const ByteBuffer& bbP, const ByteBuffer& bbE);

	Height get_ProofKernel(Merkle::Proof&, TxKernel::Ptr*, const Merkle::Hash& idKrn);

	void CommitDB();
	void EnumCongestions(uint32_t nMaxBlocksBacklog);
	static bool IsRemoteTipNeeded(const Block::SystemState::Full& sTipRemote, const Block::SystemState::Full& sTipMy);

	virtual void RequestData(const Block::SystemState::ID&, bool bBlock, const PeerID* pPreferredPeer, Height hTarget) {}
	virtual void OnPeerInsane(const PeerID&) {}
	virtual void OnNewState() {}
	virtual void OnRolledBack() {}
	virtual bool VerifyBlock(const Block::BodyBase&, TxBase::IReader&&, const HeightRange&);
	virtual void AdjustFossilEnd(Height&) {}
	virtual bool OpenMacroblock(Block::BodyBase::RW&, const NodeDB::StateID&) { return false; }
	virtual void OnModified() {}

	struct IKeyWalker {
		virtual bool OnKey(Key::IPKdf&, Key::Index) = 0;
	};
	virtual bool EnumViewerKeys(IKeyWalker&) { return true; }

	uint64_t FindActiveAtStrict(Height);

	bool ValidateTxContext(const Transaction&); // assuming context-free validation is already performed, but 
	bool ValidateTxWrtHeight(const Transaction&) const;

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
	void DeleteOutdated(TxPool::Fluff&);

	bool GetBlock(const NodeDB::StateID&, ByteBuffer& bbEthernal, ByteBuffer& bbPerishable, Height hLo0, Height hLo1, Height hHi1);

	struct UtxoRecoverSimple
		:public IUtxoWalker
	{
		std::vector<Key::IPKdf::Ptr> m_vKeys;

		UtxoRecoverSimple(NodeProcessor& x) :IUtxoWalker(x) {}

		bool Proceed();

		virtual bool OnInput(const Input&) override;
		virtual bool OnOutput(const Output&) override;

		virtual bool OnOutput(uint32_t iKey, const Key::IDV&, const Output&) = 0;
	};

	struct UtxoRecoverEx
		:public UtxoRecoverSimple
	{
		struct Value {
			Key::IDV m_Kidv;
			uint32_t m_iKey;
			Input::Count m_Count;

			Value() :m_Count(0) {}
		};
		
		typedef std::map<ECC::Point, Value> UtxoMap;
		UtxoMap m_Map;

		UtxoRecoverEx(NodeProcessor& x) :UtxoRecoverSimple(x) {}

		virtual bool OnInput(const Input&) override;
		virtual bool OnOutput(uint32_t iKey, const Key::IDV&, const Output&) override;
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

	virtual void OnUtxoEvent(const UtxoEvent::Key&, const UtxoEvent::Value&) {}
	virtual void OnDummy(const Key::ID&, Height) {}

	static bool IsDummy(const Key::IDV&);

private:
	size_t GenerateNewBlockInternal(BlockContext&);
	void GenerateNewHdr(BlockContext&);
	DataStatus::Enum OnStateInternal(const Block::SystemState::Full&, Block::SystemState::ID&);
};



} // namespace beam
