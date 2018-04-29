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

	Height m_Horizon;

public:

	typedef NodeDB::PeerID PeerID;

	void Initialize(const char* szPath, Height horizon);

	bool get_CurrentState(Block::SystemState::ID&); // returns false if no valid states so far
	bool get_CurrentState(Block::SystemState::Full&);

	void OnState(const Block::SystemState::Full&, const Block::PoW&, const PeerID&);
	bool OnBlock(const Block::SystemState::ID&, const Block::Body&, const PeerID&); // returns false if irrelevant (no known corresponding state)

	virtual void RequestState(const Block::SystemState::ID&) {} // header + PoW
	virtual void RequestBody(const Block::SystemState::ID&) {}
	virtual void OnPeerInsane(const PeerID&) {}
	virtual void OnNewState() {}
};



} // namespace beam
