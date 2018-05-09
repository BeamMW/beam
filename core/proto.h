#pragma once

#include "common.h"
#include "async.h"

namespace beam {
namespace proto {

	struct NewTip { // also the first message sent by the Node
		static const uint8_t s_Code = 1;
		Block::SystemState::ID m_ID;
	};

	struct GetHdr {
		static const uint8_t s_Code = 2;
		Block::SystemState::ID m_ID;
	};

	struct Hdr {
		static const uint8_t s_Code = 3;
		Block::SystemState::Full m_Descr;
		// PoW!
	};

	struct IsHasBody { // may be sent to multiple peers, before actually downloading the body from one peer
		static const uint8_t s_Code = 4;
		Block::SystemState::ID m_ID;
	};

	struct Boolean {
		static const uint8_t s_Code = 5;
		bool m_Value;
	};

	struct GetBody {
		static const uint8_t s_Code = 6;
		Block::SystemState::ID m_ID;
	};

	struct Body {
		static const uint8_t s_Code = 7;
		ByteBuffer m_Buf;
	};

	struct GetProofState {
		static const uint8_t s_Code = 8;
		Height m_Height;
	};

	struct GetProofKernel {
		static const uint8_t s_Code = 9;
		Merkle::Hash m_KrnHash;
	};

	struct GetProofUtxo {
		static const uint8_t s_Code = 10;
		Input m_Utxo;
		Height m_MaturityMin; // set to non-zero in case the result is too big, and should be retrieved within multiple queries
	};

	struct Proof { // for states and kernels
		static const uint8_t s_Code = 11;
		Block::SystemState::ID m_ID;
		Merkle::Proof m_Proof;
	};

	struct ProofUtxo
	{
		static const uint8_t s_Code = 12;

		Block::SystemState::ID m_ID;

		struct Entry {
			Height m_Maturity;
			Input::Count m_Count;
			Merkle::Proof m_Proof;
		};

		std::vector<Entry> m_Entries;
		static const uint32_t s_EntriesMax = 20; // if this is the size of the vector - ther result it probably trunacted
	};


} // namespace proto
} // namespace beam
