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

	void TryGoUp();

	bool GoForward(const NodeDB::StateID&);
	void Rollback(const NodeDB::StateID&);
	void PruneOld(Height);

	bool HandleBlock(const NodeDB::StateID&, NodeDB::PeerID&, bool bFwd);
	bool HandleValidatedTx(const TxBase&, Height, bool bFwd, bool bAutoAdjustInp);

	bool HandleBlockElement(Input&, bool bFwd, bool bAutoAdjustInp);
	bool HandleBlockElement(const Output&, Height, bool bFwd);
	bool HandleBlockElement(const TxKernel&, bool bFwd);

	Height m_Horizon;

	void OnCorrupted();

	std::list<Transaction::Ptr> m_lstCurrentlyMining;
	struct BlockBulder;

public:

	typedef NodeDB::PeerID PeerID;

	void Initialize(const char* szPath, Height horizon);

	bool get_CurrentState(Block::SystemState::ID&); // returns false if no valid states so far
	bool get_CurrentState(Block::SystemState::Full&);

	bool OnState(const Block::SystemState::Full&, const NodeDB::Blob& pow, const PeerID&);
	bool OnBlock(const Block::SystemState::ID&, const NodeDB::Blob& block, const PeerID&); // returns false if irrelevant (no known corresponding state)

	virtual void RequestState(const Block::SystemState::ID&) {} // header + PoW
	virtual void RequestBody(const Block::SystemState::ID&) {}
	virtual void OnPeerInsane(const PeerID&) {}
	virtual void OnNewState() {}

	// Mining simulation
	bool FeedTransaction(Transaction::Ptr&&); // returns false if the transaction isn't valid in its context
	void SimulateMinedBlock(Block::SystemState::Full&, ByteBuffer& block, ByteBuffer& pow);

	virtual void get_Key(ECC::Scalar::Native&, Height h, bool bCoinbase) = 0;
};



} // namespace beam
