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
		}

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
		}

		if (ShouldVerify(iV) && !(txb.m_Offset.m_Value == Zero))
			m_Sigma += ECC::Context::get().G * txb.m_Offset;

		assert(!m_Height.IsEmpty());
	}

	void TxBase::Context::TestSigma()
	{
		if (m_Sigma != Zero)
			Exc::Fail("Sigma nnz");
	}

	void TxBase::Context::TestValidTransaction()
	{
		if (m_Stats.m_Coinbase != Zero)
			// PoW mode: regular transactions should not produce coinbase outputs, only the miner should do this.
			// Pbt mode: m_Coinbase is the diff between supposed and actual coinbase value. Hence it should be 0
			Exc::Fail("Coinbase in tx");

		AmountBig::AddTo(m_Sigma, m_Stats.m_Fee);
		TestSigma();
	}

	void TxBase::Context::TestValidBlock()
	{
		const auto& r = Rules::get();

		AmountBig::Number subsTotal;

		bool bPbft = (Rules::Consensus::Pbft == r.m_Consensus);
		if (bPbft)
			subsTotal = m_Stats.m_Fee; // the value subtracted from the circulation
		else
		{
			r.get_Emission(subsTotal, m_Height);
			m_Sigma = -m_Sigma;
		}

		AmountBig::AddTo(m_Sigma, subsTotal);
		TestSigma();

		if (!m_Params.m_bAllowUnsignedOutputs)
		{
			bool bHasEnoughLocked = true;
			bool bShortRange = (m_Height.m_Max - m_Height.m_Min < r.Maturity.Coinbase); // All the supposed coinbase UTXOs must be included (i.e. couldn't be spent and cut-through)

			if (bPbft)
			{
				if (bShortRange)
					bHasEnoughLocked = (m_Stats.m_Coinbase == Zero);

				// for long range we don't really know which part of the coinbase could be spent already, hence we omit this verification
			}
			else
			{
				if (bShortRange)
					bHasEnoughLocked = (m_Stats.m_Coinbase == subsTotal);
				else
				{
					// calculate the minimum coinbase value that's supposed still to be locked
					HeightRange hr;
					hr.m_Min = m_Height.m_Max - r.Maturity.Coinbase;
					hr.m_Max = m_Height.m_Max;
					r.get_Emission(subsTotal, hr);

					bHasEnoughLocked = (m_Stats.m_Coinbase >= subsTotal);

				}
			}

			if (!bHasEnoughLocked)
				Exc::Fail("Coinbase value mismatch");

		}
	}

} // namespace beam
