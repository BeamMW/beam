#pragma once

#include <boost/intrusive/set.hpp>
#include "../core/common.h"
#include "../core/storage.h"
#include "node_db.h"

namespace beam {

class NodeProcessor
{
	NodeDB m_DB;
	UtxoTree m_Utxos;
	RadixHashOnlyTree m_Kernels;

	struct DbType {
		static const uint8_t Utxo	= 0;
		static const uint8_t Kernel	= 1;
	};

	void TryGoUp();

	bool GoForward(uint64_t);
	void Rollback();
	void PruneOld();
	void DereferenceFossilBlock(uint64_t);

	struct RollbackData;

	bool HandleBlock(const NodeDB::StateID&, bool bFwd);
	bool HandleValidatedTx(TxBase::IReader&&, Height, bool bFwd, RollbackData&, const Height* = NULL);
	void AdjustCumulativeParams(const Block::BodyBase&, bool bFwd);
	bool HandleBlockElement(const Input&, Height, const Height*, bool bFwd, RollbackData&);
	bool HandleBlockElement(const Output&, Height, const Height*, bool bFwd);
	bool HandleBlockElement(const TxKernel&, bool bFwd, bool bIsInput);
	void OnSubsidyOptionChanged(bool);

	static void SquashOnce(std::vector<Block::Body>&);

	void InitCursor();
	static void OnCorrupted();
	void get_Definition(Merkle::Hash&, const Merkle::Hash& hvHist);
	uint64_t FindActiveAtStrict(Height);
	bool IsRelevantHeight(Height);
	uint8_t get_NextDifficulty();
	Timestamp get_MovingMedian();

	struct UtxoSig;
	struct UnspentWalker;

public:

	void Initialize(const char* szPath);

	struct Horizon {

		Height m_Branching; // branches behind this are pruned
		Height m_Schwarzschild; // original blocks begind this are erased

		Horizon(); // by default both are disabled.

	} m_Horizon;

	struct Cursor
	{
		// frequently used data
		NodeDB::StateID m_Sid;
		Block::SystemState::ID m_ID;
		Block::SystemState::Full m_Full;
		Merkle::Hash m_History;
		Merkle::Hash m_HistoryNext;
		uint8_t m_DifficultyNext;
		bool m_SubsidyOpen;

	} m_Cursor;

	void get_CurrentLive(Merkle::Hash&);

	// Export compressed history elements. Suitable only for "small" ranges, otherwise may be both time & memory consumng.
	void ExtractBlockWithExtra(Block::Body&, const NodeDB::StateID&);
	void ExportMacroBlock(Block::BodyBase::IMacroWriter&, const HeightRange&);
	void ExportHdrRange(const HeightRange&, Block::SystemState::Sequence::Prefix&, std::vector<Block::SystemState::Sequence::Element>&);
	void ExportMacroBlock(Block::BodyBase::IMacroWriter&);

	bool ImportMacroBlock(Block::BodyBase::IMacroReader&);

	struct DataStatus {
		enum Enum {
			Accepted,
			Rejected, // duplicated or irrelevant
			Invalid
		};
	};

	DataStatus::Enum OnState(const Block::SystemState::Full&, const PeerID&);
	DataStatus::Enum OnBlock(const Block::SystemState::ID&, const NodeDB::Blob& block, const PeerID&);

	// use only for data retrieval for peers
	NodeDB& get_DB() { return m_DB; }
	UtxoTree& get_Utxos() { return m_Utxos; }
	RadixHashOnlyTree& get_Kernels() { return m_Kernels; }

	void EnumCongestions();

	virtual void RequestData(const Block::SystemState::ID&, bool bBlock, const PeerID* pPreferredPeer) {}
	virtual void OnPeerInsane(const PeerID&) {}
	virtual void OnNewState() {}
	virtual void OnRolledBack() {}
	virtual bool VerifyBlock(const Block::BodyBase&, TxBase::IReader&&, const HeightRange&);

	bool IsStateNeeded(const Block::SystemState::ID&);

	ECC::Kdf m_Kdf;

	struct TxPool
	{
		struct Element
		{
			Transaction::Ptr m_pValue;

			struct Tx
				:public boost::intrusive::set_base_hook<>
			{
				Transaction::KeyType m_Key;

				bool operator < (const Tx& t) const { return m_Key < t.m_Key; }
				IMPLEMENT_GET_PARENT_OBJ(Element, m_Tx)
			} m_Tx;

			struct Profit
				:public boost::intrusive::set_base_hook<>
			{
				Amount m_Fee;
				size_t m_nSize;

				bool operator < (const Profit& t) const;

				IMPLEMENT_GET_PARENT_OBJ(Element, m_Profit)
			} m_Profit;

			struct Threshold
				:public boost::intrusive::set_base_hook<>
			{
				Height m_Value;

				bool operator < (const Threshold& t) const { return m_Value < t.m_Value; }

				IMPLEMENT_GET_PARENT_OBJ(Element, m_Threshold)
			} m_Threshold;
		};

		typedef boost::intrusive::multiset<Element::Tx> TxSet;
		typedef boost::intrusive::multiset<Element::Profit> ProfitSet;
		typedef boost::intrusive::multiset<Element::Threshold> ThresholdSet;

		TxSet m_setTxs;
		ProfitSet m_setProfit;
		ThresholdSet m_setThreshold;

		void AddValidTx(Transaction::Ptr&&, const Transaction::Context&, const Transaction::KeyType&);
		void Delete(Element&);
		void Clear();

		void DeleteOutOfBound(Height);
		void ShrinkUpTo(uint32_t nCount);

		~TxPool() { Clear(); }

	};

	bool ValidateTx(const Transaction&, Transaction::Context&); // wrt height of the next block

	bool GenerateNewBlock(TxPool&, Block::SystemState::Full&, ByteBuffer&, Amount& fees, Block::Body& blockInOut);
	bool GenerateNewBlock(TxPool&, Block::SystemState::Full&, ByteBuffer&, Amount& fees);

private:
	bool GenerateNewBlock(TxPool&, Block::SystemState::Full&, Block::Body& block, Amount& fees, Height, RollbackData&);
	bool GenerateNewBlock(TxPool&, Block::SystemState::Full&, ByteBuffer&, Amount& fees, Block::Body&, bool bInitiallyEmpty);
	DataStatus::Enum OnStateInternal(const Block::SystemState::Full&, Block::SystemState::ID&);
};



} // namespace beam
