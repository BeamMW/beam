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

#include "block_crypt.h"

namespace beam
{
	/////////////
	// Transaction
	void TxBase::Context::Reset()
	{
		m_Sigma = Zero;

		ZeroObject(m_Fee);
		ZeroObject(m_Coinbase);
		m_Height.Reset();
		m_bBlockMode = false;
		m_bVerifyOrder = true;
		m_nVerifiers = 1;
		m_iVerifier = 0;
		m_pAbort = NULL;
	}

	bool TxBase::Context::ShouldVerify(uint32_t& iV) const
	{
		if (iV)
		{
			iV--;
			return false;
		}

		iV = m_nVerifiers - 1;
		return true;
	}

	bool TxBase::Context::ShouldAbort() const
	{
		return m_pAbort && *m_pAbort;
	}

	bool TxBase::Context::HandleElementHeight(const HeightRange& hr)
	{
		HeightRange r = m_Height;
		r.Intersect(hr);
		if (r.IsEmpty())
			return false;

		if (!m_bBlockMode)
			m_Height = r; // shrink permitted range

		return true;
	}

	bool TxBase::Context::Merge(const Context& x)
	{
		assert(m_bBlockMode == x.m_bBlockMode);

		if (!HandleElementHeight(x.m_Height))
			return false;

		m_Sigma += x.m_Sigma;
		m_Fee += x.m_Fee;
		m_Coinbase += x.m_Coinbase;
		return true;
	}

	bool TxBase::Context::ValidateAndSummarize(const TxBase& txb, IReader&& r)
	{
		if (m_Height.IsEmpty())
			return false;

		m_Sigma = -m_Sigma;

		assert(m_nVerifiers);
		uint32_t iV = m_iVerifier;

		// Inputs
		r.Reset();

		ECC::Point::Native pt;

		for (const Input* pPrev = NULL; r.m_pUtxoIn; pPrev = r.m_pUtxoIn, r.NextUtxoIn())
		{
			if (ShouldAbort())
				return false;

			if (ShouldVerify(iV))
			{
				if (m_bVerifyOrder)
				{
					if (pPrev && (*pPrev > *r.m_pUtxoIn))
						return false;

					// make sure no redundant outputs
					for (; r.m_pUtxoOut; r.NextUtxoOut())
					{
						int n = CmpInOut(*r.m_pUtxoIn, *r.m_pUtxoOut);
						if (n < 0)
							break;

						if (!n)
							return false; // duplicate!
					}
				}

				if (!pt.Import(r.m_pUtxoIn->m_Commitment))
					return false;

				m_Sigma += pt;
			}
		}

		m_Sigma = -m_Sigma;

		// Outputs
		r.Reset();

		for (const Output* pPrev = NULL; r.m_pUtxoOut; pPrev = r.m_pUtxoOut, r.NextUtxoOut())
		{
			if (ShouldAbort())
				return false;

			if (ShouldVerify(iV))
			{
				if (m_bVerifyOrder && pPrev && (*pPrev > *r.m_pUtxoOut))
					return false;

				if (!r.m_pUtxoOut->IsValid(pt))
					return false;

				m_Sigma += pt;

				if (r.m_pUtxoOut->m_Coinbase)
				{
					if (!m_bBlockMode)
						return false; // regular transactions should not produce coinbase outputs, only the miner should do this.

					assert(r.m_pUtxoOut->m_pPublic); // must have already been checked
					m_Coinbase += r.m_pUtxoOut->m_pPublic->m_Value;
				}
			}
		}

		for (const TxKernel* pPrev = NULL; r.m_pKernel; pPrev = r.m_pKernel, r.NextKernel())
		{
			if (ShouldAbort())
				return false;

			if (ShouldVerify(iV))
			{
				if (m_bVerifyOrder && pPrev && (*pPrev > *r.m_pKernel))
					return false;

				if (!r.m_pKernel->IsValid(m_Fee, m_Sigma))
					return false;

				if (!HandleElementHeight(r.m_pKernel->m_Height))
					return false;
			}
		}

		if (ShouldVerify(iV))
			m_Sigma += ECC::Context::get().G * txb.m_Offset;

		assert(!m_Height.IsEmpty());
		return true;
	}

	bool TxBase::Context::IsValidTransaction()
	{
		assert(!(m_Coinbase.Lo || m_Coinbase.Hi)); // must have already been checked

		m_Fee.AddTo(m_Sigma);

		return m_Sigma == Zero;
	}

	bool TxBase::Context::IsValidBlock(const Block::BodyBase& bb, bool bSubsidyOpen)
	{
		m_Sigma = -m_Sigma;

		bb.m_Subsidy.AddTo(m_Sigma);

		if (!(m_Sigma == Zero))
			return false;

		if (bSubsidyOpen)
			return true;

		if (bb.m_SubsidyClosing)
			return false; // already closed

		// For non-genesis blocks we have the following restrictions:
		// Subsidy is bounded by num of blocks multiplied by coinbase emission
		// There must at least some unspent coinbase UTXOs wrt maturity settings

		// check the subsidy is within allowed range
		Height nBlocksInRange = m_Height.m_Max - m_Height.m_Min + 1;

		AmountBig::uintBig ubSubsidy;
		bb.m_Subsidy.Export(ubSubsidy);

		auto ubBlockEmission = uintBigFrom(Rules::get().CoinbaseEmission);

		if (ubSubsidy > uintBigFrom(nBlocksInRange) * ubBlockEmission)
			return false;

		// ensure there's a minimal unspent coinbase UTXOs
		if (nBlocksInRange > Rules::get().MaturityCoinbase)
		{
			// some UTXOs may be spent already. Calculate the minimum remaining
			auto ubCoinbaseMaxSpent = uintBigFrom(nBlocksInRange - Rules::get().MaturityCoinbase) * ubBlockEmission;

			if (ubSubsidy > ubCoinbaseMaxSpent)
			{
				ubCoinbaseMaxSpent.Negate();
				ubSubsidy += ubCoinbaseMaxSpent;

			} else
				ubSubsidy = Zero;
		}

		AmountBig::uintBig ubCoinbase;
		m_Coinbase.Export(ubCoinbase);
		return (ubCoinbase >= ubSubsidy);
	}

} // namespace beam
