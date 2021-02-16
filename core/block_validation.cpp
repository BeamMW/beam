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
	TxBase::Context::Params::Params()
	{
		ZeroObject(*this);
		m_nVerifiers = 1;
	}

	void TxBase::Context::Reset()
	{
		m_Sigma = Zero;
		m_Stats.Reset();
		m_Height.Reset();
		m_iVerifier = 0;
	}

	bool TxBase::Context::ShouldVerify(uint32_t& iV) const
	{
		if (iV)
		{
			iV--;
			return false;
		}

		iV = m_Params.m_nVerifiers - 1;
		return true;
	}

	bool TxBase::Context::ShouldAbort() const
	{
		return m_Params.m_pAbort && *m_Params.m_pAbort;
	}

	bool TxBase::Context::HandleElementHeight(const HeightRange& hr)
	{
		m_Height.Intersect(hr); // shrink permitted range. For blocks have no effect, since the range must already consist of a single height
		return !m_Height.IsEmpty();
	}

	bool TxBase::Context::Merge(const Context& x)
	{
		if (!HandleElementHeight(x.m_Height))
			return false;

		m_Sigma += x.m_Sigma;
		m_Stats += x.m_Stats;
		return true;
	}

	bool TxBase::Context::ValidateAndSummarize(const TxBase& txb, IReader&& r)
	{
		if (m_Height.IsEmpty())
			return false;

		const Rules& rules = Rules::get(); // alias

		auto iFork = rules.FindFork(m_Height.m_Min);
		std::setmin(m_Height.m_Max, rules.get_ForkMaxHeightSafe(iFork)); // mixed versions are not allowed!
		assert(!m_Height.IsEmpty());

		ECC::Mode::Scope scope(ECC::Mode::Fast);

		m_Sigma = -m_Sigma;

		assert(m_Params.m_nVerifiers);
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

				if (!pt.Import(r.m_pUtxoIn->m_Commitment))
					return false;

				r.m_pUtxoIn->AddStats(m_Stats);
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
				if (pPrev && (*pPrev > *r.m_pUtxoOut))
				{
					// in case of unsigned outputs sometimes order of outputs may look incorrect (duplicated commitment, part of signatures removed)
					if (!m_Params.m_bAllowUnsignedOutputs || (pPrev->m_Commitment != r.m_pUtxoOut->m_Commitment))
						return false;
				}

				bool bSigned = r.m_pUtxoOut->m_pConfidential || r.m_pUtxoOut->m_pPublic;

				if (bSigned)
				{
					if (!r.m_pUtxoOut->IsValid(m_Height.m_Min, pt))
						return false;
				}
				else
				{
					// unsigned output
					if (!m_Params.m_bAllowUnsignedOutputs)
						return false;

					if (!pt.Import(r.m_pUtxoOut->m_Commitment))
						return false;
				}

				r.m_pUtxoOut->AddStats(m_Stats);
				m_Sigma += pt;
			}
		}

		for (const TxKernel* pPrev = NULL; r.m_pKernel; pPrev = r.m_pKernel, r.NextKernel())
		{
			if (ShouldAbort())
				return false;

			if (ShouldVerify(iV))
			{
				if (pPrev && ((*pPrev) > (*r.m_pKernel)))
					return false; // wrong order

				if (!r.m_pKernel->IsValid(m_Height.m_Min, m_Sigma))
					return false;

				HeightRange hr = r.m_pKernel->m_Height;
				if (iFork >= 2)
				{
					if (!hr.IsEmpty() && (hr.m_Max - hr.m_Min > rules.MaxKernelValidityDH))
						hr.m_Max = hr.m_Min + rules.MaxKernelValidityDH;
				}

				if (!HandleElementHeight(hr))
					return false;

				r.m_pKernel->AddStats(m_Stats);
			}
		}

		if (ShouldVerify(iV) && !(txb.m_Offset.m_Value == Zero))
			m_Sigma += ECC::Context::get().G * txb.m_Offset;

		assert(!m_Height.IsEmpty());
		return true;
	}

	bool TxBase::Context::IsValidTransaction()
	{
		if (m_Stats.m_Coinbase != Zero)
			return false; // regular transactions should not produce coinbase outputs, only the miner should do this.

		AmountBig::AddTo(m_Sigma, m_Stats.m_Fee);
		return m_Sigma == Zero;
	}

	bool TxBase::Context::IsValidBlock()
	{
		AmountBig::Type subsTotal, subsLocked;
		Rules::get_Emission(subsTotal, m_Height);

		m_Sigma = -m_Sigma;

		AmountBig::AddTo(m_Sigma, subsTotal);

		if (!(m_Sigma == Zero))
			return false;

		if (!m_Params.m_bAllowUnsignedOutputs)
		{
			// Subsidy is bounded by num of blocks multiplied by coinbase emission
			// There must at least some unspent coinbase UTXOs wrt maturity settings
			if (m_Height.m_Max - m_Height.m_Min < Rules::get().Maturity.Coinbase)
				subsLocked = subsTotal;
			else
			{
				HeightRange hr;
				hr.m_Min = m_Height.m_Max - Rules::get().Maturity.Coinbase;
				hr.m_Max = m_Height.m_Max;
				Rules::get_Emission(subsLocked, hr);
			}

			if (m_Stats.m_Coinbase < subsLocked)
				return false;
		}

		return true;
	}

} // namespace beam
