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

	void OnCorrupted();
	void get_CurrentLive(Merkle::Hash&);

	static void get_KrnKey(Merkle::Hash&, const TxKernel&);

	std::list<Transaction::Ptr> m_lstCurrentlyMining;
	struct BlockBulder;

	bool IsRelevantHeight(Height);

public:

	typedef NodeDB::PeerID PeerID;

	void Initialize(const char* szPath);

	struct Horizon {

		Height m_Branching; // branches behind this are pruned
		Height m_Schwarzschild; // original blocks begind this are erased

		Horizon(); // by default both are disabled.

	} m_Horizon;


	bool get_CurrentState(Block::SystemState::ID&); // returns false if no valid states so far
	bool get_CurrentState(Block::SystemState::Full&);

	//  both functions return true if dirty (i.e. data is relevant, and added)
	bool OnState(const Block::SystemState::Full&, const NodeDB::Blob& pow, const PeerID&);
	bool OnBlock(const Block::SystemState::ID&, const NodeDB::Blob& block, const PeerID&);

	NodeDB& get_DB() { return m_DB; } // use only for data retrieval for peers

	void EnumCongestions();

	virtual void RequestData(const Block::SystemState::ID&, bool bBlock, const PeerID* pPreferredPeer) {}
	virtual void OnPeerInsane(const PeerID&) {}
	virtual void OnNewState() {}

	// Mining simulation
	bool FeedTransaction(Transaction::Ptr&&); // returns false if the transaction isn't valid in its context
	void SimulateMinedBlock(Block::SystemState::Full&, ByteBuffer& block, ByteBuffer& pow);

	bool IsStateNeeded(const Block::SystemState::ID&);

	struct KeyType {
		enum Enum {
			Comission,
			Coinbase,
			Kernel
		};
	};

	ECC::Kdf m_Kdf;
	static void DeriveKey(ECC::Scalar::Native&, const ECC::Kdf&, Height, KeyType::Enum, uint32_t nIdx = 0);

protected:
	virtual void OnMined(Height, Amount nFee) {}
};



} // namespace beam
