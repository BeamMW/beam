#pragma once

#include "common.h"
#include "async.h"

//////////////////////////
// Protocol
//
//	get_Tip: Getting the tip, state of the active branch
//		<- State
//
//	get_PoW: Getting a range of consecutive system states.
//		-> StateTop, CountBack
//		<- vector<State>
//
//	get_PoW: Getting PoW for a specific state transition
//		-> State
//		<- ptr<PoW>, NULL if state not found (invalid or deleted as dead)
//
//	get_ProofState: Getting a proof that a specific state is included in the blockchain
//		-> State (not necessarily tip), StateParent
//		<- Proof, empty if invalid
//
//	get_ProofUtxo: Getting proof of the unspent UTXO w.r.t. current tip (not supporting w.r.t. atbitrary state!)
//		-> ECC::Point
//		<- vector<BlockHeight, CoinBaseFlag, Count, Proof>
//
//	get_ProofKernel: Getting a proof for a specific TxKernel. The State must be specified (not supporting auto-search). For simplicity node should support search in a limited range
//		-> State, CountSearchBack
//		<- State, Proof, empty if not found
//

#define Beam_ProtoMsgs_ToNode(macro) \
	macro(get_Tip) \
	macro(get_StateRange) \
	macro(get_PoW) \
	macro(get_ProofState) \
	macro(get_ProofUtxo) \
	macro(get_ProofKernel) \
	macro(HandleTransaction)


#define Beam_ProtoMsgs_FromNode(macro) \
	macro(ret_Tip) \
	macro(ret_StateRange) \
	macro(ret_PoW) \
	macro(ret_ProofState) \
	macro(ret_ProofUtxo) \
	macro(ret_ProofKernel) \
	macro(HandleTransactionRes)

#define Beam_ProtoMsg_get_Tip(macro)

#define Beam_ProtoMsg_ret_Tip(macro) \
	macro(beam::Block::SystemState, State)

#define Beam_ProtoMsg_get_StateRange(macro) \
	macro(beam::Block::SystemStateShort, StateTop) \
	macro(beam::Height, CountBack)

#define Beam_ProtoMsg_ret_StateRange(macro) \
	macro(std::vector<beam::Block::SystemState>, States)

#define Beam_ProtoMsg_get_PoW(macro) \
	macro(beam::Block::SystemStateShort, State)

#define Beam_ProtoMsg_ret_PoW(macro) \
	macro(std::unique_ptr<beam::Block::PoW>, PoW)

#define Beam_ProtoMsg_get_ProofState(macro) \
	macro(beam::Block::SystemStateShort, State) \
	macro(beam::Block::SystemStateShort, Parent)

#define Beam_ProtoMsg_ret_ProofState(macro) \
	macro(beam::Merkle::Proof, Proof)

#define Beam_ProtoMsg_get_ProofUtxo(macro) \
	macro(ECC::Point, Utxo) \

#define Beam_ProtoMsg_ret_ProofUtxo(macro) \
	macro(beam::Block::SystemStateShort, Tip) \
	macro(std::vector<beam::proto::UtxoProof>, Proofs)

#define Beam_ProtoMsg_get_ProofKernel(macro) \
	macro(beam::Block::SystemStateShort, State) \
	macro(beam::TxKernel, Kernel)

#define Beam_ProtoMsg_ret_ProofKernel(macro) \
	macro(beam::Merkle::Proof, Proof)

#define Beam_ProtoMsg_ret_HandleTransaction(macro) \
	macro(beam::Transaction, Transaction)

#define HandleTransactionRes(macro) \
	macro(bool, IsValid)

namespace beam {
namespace proto {

	struct UtxoDescription {
		uint64_t	m_Height;
		uint32_t	m_Count;
		bool		n_Coinbase;
	};

	struct UtxoProof {
		UtxoDescription	m_Description;
		Merkle::Proof	m_Proof;
	};

} // namespace proto
} // namespace beam
