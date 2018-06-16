#pragma once
#include "common.h"
#include "ecc_native.h"

namespace beam
{
	class TxBase::Context
	{
		bool ShouldVerify(uint32_t& iV) const;
		bool ShouldAbort() const;

		bool HandleElementHeight(const HeightRange&);

	public:
		// Tests the validity of all the components, overall arithmetics, and the lexicographical order of the components.
		// Determines the min/max block height that the transaction can fit, wrt component heights and maturity policies
		// Does *not* check the existence of the input UTXOs
		//
		// Validation formula
		//
		// Sum(Input UTXOs) + Sum(Input Kernels.Excess) = Sum(Output UTXOs) + Sum(Output Kernels.Excess) + m_Offset*G [ + Sum(Fee)*H ]
		//
		// For transaction validation fees are considered as implicit outputs (i.e. Sum(Fee)*H should be added for the right equation side)
		//
		// For a block validation Fees are not accounted for, since they are consumed by new outputs injected by the miner.
		// However Each block contains extra outputs (coinbase) for block closure, which should be subtracted from the outputs for sum validation.
		//
		// Define: Sigma = Sum(Output UTXOs) - Sum(Input UTXOs) + Sum(Output Kernels.Excess) - Sum(Input Kernels.Excess) + m_Offset*G
		// In other words Sigma = <all outputs> - <all inputs>
		// Sigma is either zero or -Sum(Fee)*H, depending on what we validate


		ECC::Point::Native m_Sigma;

		AmountBig m_Fee;
		AmountBig m_Coinbase;
		HeightRange m_Height;

		bool m_bBlockMode; // in 'block' mode the hMin/hMax on input denote the range of heights. Each element is verified wrt it independently.
		// i.e. different elements may have non-overlapping valid range, and it's valid.
		// Suitable for merged block validation

		// for multi-tasking, parallel verification
		uint32_t m_nVerifiers;
		uint32_t m_iVerifier;
		volatile bool* m_pAbort;

		Context() { Reset(); }
		void Reset();

		bool ValidateAndSummarize(IReader&);
		bool Merge(const Context&);

		// hi-level functions, should be used after all parts were validated and merged
		bool IsValidTransaction();
		bool IsValidBlock(Block::Body::IReader&, bool bSubsidyOpen);
	};
}
