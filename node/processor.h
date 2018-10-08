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
#include "db.h"
#include "txpool.h"

namespace beam {

class NodeProcessor
{
	NodeDB m_DB;
	UtxoTree m_Utxos;
	RadixHashOnlyTree m_Kernels;

	bool m_bShallowTx = false;

	struct ShallowTx
	{
		bool* m_pDst;

		ShallowTx(NodeProcessor& p)
		{
			if (p.m_bShallowTx)
				m_pDst = NULL;
			else
				m_pDst = &(p.m_bShallowTx = true);
		}

		~ShallowTx()
		{
			if (m_pDst)
				*m_pDst = false;
		}
	};

	struct DbType {
		static const uint8_t Utxo	= 0;
		static const uint8_t Kernel	= 1;
	};

	void TryGoUp();

	bool GoForward(uint64_t);
	void Rollback();
	void PruneOld();
	void PruneAt(Height, bool bDeleteBody);
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
	void get_Definition(Merkle::Hash&, bool bForNextState);
	Difficulty get_NextDifficulty();
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
		Difficulty m_DifficultyNext;
		bool m_SubsidyOpen;
		Height m_LoHorizon; // lowest accessible height

	} m_Cursor;

	void get_CurrentLive(Merkle::Hash&);

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
	DataStatus::Enum OnBlock(const Block::SystemState::ID&, const NodeDB::Blob& block, const PeerID&);

	// use only for data retrieval for peers
	NodeDB& get_DB() { return m_DB; }
	UtxoTree& get_Utxos() { return m_Utxos; }
	RadixHashOnlyTree& get_Kernels() { return m_Kernels; }

	bool get_KernelHashPreimage(const Merkle::Hash& id, ECC::uintBig&);

	void EnumCongestions();
	static bool IsRemoteTipNeeded(const Block::SystemState::Full& sTipRemote, const Block::SystemState::Full& sTipMy);

	virtual void RequestData(const Block::SystemState::ID&, bool bBlock, const PeerID* pPreferredPeer) {}
	virtual void OnPeerInsane(const PeerID&) {}
	virtual void OnNewState() {}
	virtual void OnRolledBack() {}
	virtual bool VerifyBlock(const Block::BodyBase&, TxBase::IReader&&, const HeightRange&);
	virtual bool ApproveState(const Block::SystemState::ID&) { return true; }
	virtual void AdjustFossilEnd(Height&) {}
	virtual void OnStateData() {}
	virtual void OnBlockData() {}

	uint64_t FindActiveAtStrict(Height);

	ECC::Kdf m_Kdf;

	static void DeriveKeys(const ECC::Kdf&, Height, Amount fees, ECC::Scalar::Native& kCoinbase, ECC::Scalar::Native& kFee, ECC::Scalar::Native& kKernel, ECC::Scalar::Native& kOffset);

	bool ValidateTxContext(const Transaction&); // assuming context-free validation is already performed, but 
	static bool ValidateTxWrtHeight(const Transaction&, Height);

	bool GenerateNewBlock(TxPool::Fluff&, Block::SystemState::Full&, ByteBuffer&, Amount& fees, Block::Body& blockInOut);
	bool GenerateNewBlock(TxPool::Fluff&, Block::SystemState::Full&, ByteBuffer&, Amount& fees);

private:
	bool GenerateNewBlock(TxPool::Fluff&, Block::SystemState::Full&, Block::Body& block, Amount& fees, Height, RollbackData&);
	bool GenerateNewBlock(TxPool::Fluff&, Block::SystemState::Full&, ByteBuffer&, Amount& fees, Block::Body&, bool bInitiallyEmpty);
	DataStatus::Enum OnStateInternal(const Block::SystemState::Full&, Block::SystemState::ID&);
};



} // namespace beam
