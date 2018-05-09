#pragma once

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

	bool GoForward(const NodeDB::StateID&);
	void Rollback(const NodeDB::StateID&);
	void PruneOld(Height);
	void DereferenceFossilBlock(uint64_t);

	struct RollbackData;

	bool HandleBlock(const NodeDB::StateID&, bool bFwd);
	bool HandleValidatedTx(const TxBase&, Height, bool bFwd, RollbackData&);

	bool HandleBlockElement(const Input&, bool bFwd, Height, RollbackData&);
	bool HandleBlockElement(const Output&, Height, bool bFwd);
	bool HandleBlockElement(const TxKernel&, bool bFwd, bool bIsInput);

	Height m_Horizon;

	void OnCorrupted();
	void get_CurrentLive(Merkle::Hash&);

	static void get_KrnKey(Merkle::Hash&, const TxKernel&);

	std::list<Transaction::Ptr> m_lstCurrentlyMining;
	struct BlockBulder;

	bool IsRelevantHeight(Height);
	void FindCongestionPoints();
	void FindCongestionPointsAbove(NodeDB::StateID);

	void RequestDataInternal(uint64_t rowid, bool bBlock);

public:

	typedef NodeDB::PeerID PeerID;

	void Initialize(const char* szPath, Height horizon);

	bool get_CurrentState(Block::SystemState::ID&); // returns false if no valid states so far
	bool get_CurrentState(Block::SystemState::Full&);

	bool OnState(const Block::SystemState::Full&, const NodeDB::Blob& pow, const PeerID&);
	bool OnBlock(const Block::SystemState::ID&, const NodeDB::Blob& block, const PeerID&); // returns false if irrelevant (no known corresponding state)

	virtual void RequestData(const Block::SystemState::ID&, bool bBlock, const PeerID* pPreferredPeer) {}
	virtual void OnPeerInsane(const PeerID&) {}
	virtual void OnNewState() {}

	// Mining simulation
	bool FeedTransaction(Transaction::Ptr&&); // returns false if the transaction isn't valid in its context
	void SimulateMinedBlock(Block::SystemState::Full&, ByteBuffer& block, ByteBuffer& pow);

	void RealizePeerTip(const Block::SystemState::ID&, const PeerID&);

protected:
	virtual void get_Key(ECC::Scalar::Native&, Height h, bool bCoinbase) = 0;
	virtual void OnMined(Height, const ECC::Scalar::Native& kFee, Amount nFee, const ECC::Scalar::Native& kCoinbase, Amount nCoinbase) {}
};



} // namespace beam
