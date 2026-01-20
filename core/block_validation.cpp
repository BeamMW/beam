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
	void TxBase::Fail_Order() {
		Exc::Fail("Wrong Order");
	}

	void TxBase::Fail_Signature() {
		Exc::Fail("Invalid signature");
	}

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

	void TxBase::Context::TestAbort() const
	{
		if (m_Params.m_pAbort && *m_Params.m_pAbort)
			Exc::Fail("abort");
	}

	void TxBase::Context::TestHeightNotEmpty() const
	{
		if (m_Height.IsEmpty())
			Exc::Fail("Height mismatch");
	}

	void TxBase::Context::HandleElementHeightStrict(const HeightRange& hr)
	{
		m_Height.Intersect(hr); // shrink permitted range. For blocks have no effect, since the range must already consist of a single height
		TestHeightNotEmpty();
	}

	void TxBase::Context::MergeStrict(const Context& x)
	{
		HandleElementHeightStrict(x.m_Height);
		m_Sigma += x.m_Sigma;
		m_Stats += x.m_Stats;
	}

	bool TxBase::Context::ValidateAndSummarize(const TxBase& txb, IReader&& r, std::string* psErr /* = nullptr */)
	{
		try {
			ValidateAndSummarizeStrict(txb, std::move(r));
		} catch (const std::exception& e) {
			if (psErr)
				(*psErr) = e.what();
			return false;
		}
		return true;
	}

	void TxBase::Context::ValidateAndSummarizeStrict(const TxBase& txb, IReader&& r)
	{
		TestHeightNotEmpty();

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
			TestAbort();

			if (ShouldVerify(iV))
			{
				struct MyCheckpoint :public Exc::Checkpoint {

					const Input& m_Inp;
					MyCheckpoint(const Input& inp) :m_Inp(inp) {}

					void Dump(std::ostream& os) override
					{
						os << "Input " << m_Inp.m_Commitment;
					}

				} cp(*r.m_pUtxoIn);

				if (pPrev && (*pPrev > *r.m_pUtxoIn))
					Fail_Order();

				// make sure no redundant outputs
				for (; r.m_pUtxoOut; r.NextUtxoOut())
				{
					int n = CmpInOut(*r.m_pUtxoIn, *r.m_pUtxoOut);
					if (n < 0)
						break;

					if (!n)
						Exc::Fail("dup out"); // duplicate!
				}

				pt.ImportStrict(r.m_pUtxoIn->m_Commitment);

				r.m_pUtxoIn->AddStats(m_Stats);
				m_Sigma += pt;
			}
		}

		m_Sigma = -m_Sigma;

		bool bHandleCoinbase = !m_Params.m_bAllowUnsignedOutputs && ShouldVerify(iV);
		AmountBig::Number valCoinbase = Zero;

		// Outputs
		r.Reset();

		for (const Output* pPrev = NULL; r.m_pUtxoOut; pPrev = r.m_pUtxoOut, r.NextUtxoOut())
		{
			TestAbort();

			if (ShouldVerify(iV))
			{
				struct MyCheckpoint :public Exc::Checkpoint {

					const Output& m_Outp;
					MyCheckpoint(const Output& outp) :m_Outp(outp) {}

					void Dump(std::ostream& os) override
					{
						os << "Output " << m_Outp.m_Commitment;
					}

				} cp(*r.m_pUtxoOut);

				if (pPrev && (*pPrev > *r.m_pUtxoOut))
				{
					// in case of unsigned outputs sometimes order of outputs may look incorrect (duplicated commitment, part of signatures removed)
					if (!m_Params.m_bAllowUnsignedOutputs || (pPrev->m_Commitment != r.m_pUtxoOut->m_Commitment))
						Fail_Order();
				}

				bool bSigned = r.m_pUtxoOut->m_pConfidential || r.m_pUtxoOut->m_pPublic;

				if (bSigned)
				{
					if (!r.m_pUtxoOut->IsValid(m_Height.m_Min, pt))
						Fail_Signature();
				}
				else
				{
					// unsigned output
					if (!m_Params.m_bAllowUnsignedOutputs)
						Exc::Fail("Missing rangeproof");

					pt.ImportStrict(r.m_pUtxoOut->m_Commitment);
				}

				r.m_pUtxoOut->AddStats(m_Stats);
				m_Sigma += pt;
			}

			if (bHandleCoinbase && r.m_pUtxoOut->m_Coinbase)
			{
				if (!m_Params.m_bBlock)
					Exc::Fail("Coinbase in tx");

				if (Rules::Consensus::Pbft == rules.m_Consensus)
					Exc::Fail("Coinbase in pbft");

				if (r.m_pUtxoOut->m_pPublic)
					valCoinbase += AmountBig::Number(r.m_pUtxoOut->m_pPublic->m_Value);

			}
		}

		AmountBig::Number valFees = Zero;
		bool bFeesToSigma = (!m_Params.m_bBlock || (Rules::Consensus::Pbft == rules.m_Consensus)) && ShouldVerify(iV);

		for (const TxKernel* pPrev = NULL; r.m_pKernel; pPrev = r.m_pKernel, r.NextKernel())
		{
			TestAbort();

			if (ShouldVerify(iV))
			{
				TxKernel::Checkpoint cp(*r.m_pKernel);

				if (pPrev && ((*pPrev) > (*r.m_pKernel)))
					Fail_Order(); // wrong order

				r.m_pKernel->TestValid(m_Height.m_Min, m_Sigma);

				HandleElementHeightStrict(r.m_pKernel->get_EffectiveHeightRange());

				r.m_pKernel->AddStats(m_Stats);
			}

			if (bFeesToSigma)
				r.m_pKernel->AddFees(valFees);
		}

		assert(!m_Height.IsEmpty());

		if (ShouldVerify(iV) && !(txb.m_Offset.m_Value == Zero))
			m_Sigma += ECC::Context::get().G * txb.m_Offset;

		if (bFeesToSigma)
			AmountBig::AddTo(m_Sigma, valFees);

		if (m_Params.m_bBlock)
		{
			assert(m_Height.m_Min == m_Height.m_Max);

			bool bAddSubsidy =
				(Rules::Consensus::Pbft != rules.m_Consensus) &&
				ShouldVerify(iV);

			Amount subsidy = 0;

			if (bAddSubsidy || bHandleCoinbase)
				subsidy = rules.get_Emission(m_Height.m_Min);

			if (bHandleCoinbase && (AmountBig::Number(subsidy) != valCoinbase))
				Exc::Fail("Coinbase value mismatch");

			if (bAddSubsidy)
			{
				m_Sigma = -m_Sigma;
				AmountBig::AddTo(m_Sigma, subsidy);
				m_Sigma = -m_Sigma;
			}

		}
	}

	void TxBase::Context::TestSigma() const
	{
		if (m_Sigma != Zero)
			Exc::Fail("Sigma nnz");
	}


} // namespace beam
