#pragma once
#include "common.h"
#include "ecc_native.h"

namespace beam
{
	struct TxBase::Context
	{
		ECC::Point::Native m_Sigma; // outputs - inputs

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

		bool ShouldVerify(uint32_t& iV) const;
		bool ShouldAbort() const;

		Context() { Reset(); }
		void Reset();

		bool HandleElementHeight(const HeightRange&);

		bool Merge(const Context&);

		// hi-level functions, should be used after Merge (in case the verification was split)
		bool IsValidTransaction();
		bool IsValidBlock(const Block::Body&, bool bSubsidyOpen);
	};
}
