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

#include "lelantus.h"

namespace beam {
namespace Lelantus {

using namespace ECC;


void SpendKey::ToSerial(Scalar::Native& serial, const Point& pk)
{
	Oracle()
		<< "L.Spend"
		<< pk
		>> serial;
}

///////////////////////////
// Calculate "standard" vector commitments of M+1 elements
struct CommitmentStd
{
	static const uint32_t s_Generators = Cfg::M * Cfg::n + 1;

	struct MultiMacMy
		:public MultiMac_WithBufs<1, s_Generators>
	{
		MultiMacMy()
		{
			static_assert(s_Generators < InnerProduct::nDim * 2);
			const MultiMac::Prepared* pP0 = Context::get().m_Ipp.m_pGen_[0];

			for (uint32_t j = 0; j < Cfg::M; j++)
			{
				for (uint32_t i = 0; i < Cfg::n; i++, m_Prepared++)
				{
					m_ppPrepared[m_Prepared] = pP0 + m_Prepared;
				}
			}

			m_ppPrepared[m_Prepared] = pP0 + m_Prepared;
			m_Prepared++;
			assert(s_Generators == m_Prepared);
		}
	};

	virtual void get_At(Scalar::Native&, uint32_t j, uint32_t i) = 0;

	void FillEquation(MultiMac& mm, const Scalar::Native& blinding, const Scalar::Native* pMultiplier = nullptr)
	{
		uint32_t iIdx = 0;
		Scalar::Native k;

		for (uint32_t j = 0; j < Cfg::M; j++)
		{
			for (uint32_t i = 0; i < Cfg::n; i++, iIdx++)
			{
				if (pMultiplier)
				{
					get_At(k, j, i);
					mm.m_pKPrep[iIdx] += k * (*pMultiplier);
				}
				else
					get_At(mm.m_pKPrep[iIdx], j, i);
			}
		}

		if (pMultiplier)
			mm.m_pKPrep[iIdx] += blinding * (*pMultiplier);
		else
			mm.m_pKPrep[iIdx] = blinding;
	}

	void Calculate(Point& res, MultiMacMy& mm, const Scalar::Native& blinding)
	{
		FillEquation(mm, blinding);
		Point::Native resN;
		mm.Calculate(resN);

		res = resN;
	}

	// A + B*x =?= Commitment(..., z)
	bool IsValid(InnerProduct::BatchContext& bc, const Point& ptA, const Point& ptB, const Scalar::Native& x, const Scalar& z)
	{
		bc.EquationBegin();
		FillEquation(bc, z, &bc.m_Multiplier);

		Scalar::Native k = -bc.m_Multiplier;
		if (!bc.AddCasual(ptA, k, true))
			return false;

		k *= x;
		return bc.AddCasual(ptB, k, true);
	}
};


///////////////////////////
// Proof
void Proof::Part1::Expose(Oracle& oracle) const
{
	oracle
		<< m_SpendPk
		<< m_Output
		<< m_NonceG
		<< m_A
		<< m_B
		<< m_C
		<< m_D;

	for (uint32_t k = 0; k < Cfg::M; k++)
	{
		oracle
			<< m_pG[k]
			<< m_pQ[k];
	}
}

bool Proof::IsValid(Oracle& oracle, CmList& cmList) const
{
	if (InnerProduct::BatchContext::s_pInstance)
		return IsValid(*InnerProduct::BatchContext::s_pInstance, oracle, cmList);

	InnerProduct::BatchContextEx<4> bc;
	return
		IsValid(bc, oracle, cmList) &&
		bc.Flush();
}

bool Proof::IsValid(InnerProduct::BatchContext& bc, Oracle& oracle, CmList& cmList) const
{
	Mode::Scope scope(Mode::Fast);

	struct MyContext
	{
		const Proof& m_P;

		Scalar::Native pF0[Cfg::M];
		Scalar::Native x;

		void get_F(Scalar::Native& out, uint32_t j, uint32_t i) const
		{
			out = i ?
				m_P.m_Part2.m_pF[j][i - 1] :
				pF0[j];
		}

		MyContext(const Proof& p) :m_P(p) {}

	} mctx(*this);

	m_Part1.Expose(oracle);
	oracle >> mctx.x;

	Scalar::Native x2, x3;
	oracle >> x2; // balance proof challenge
	oracle >> x3; // spend proof challenge

	// recover pF0
	for (uint32_t j = 0; j < Cfg::M; j++)
	{
		mctx.pF0[j] = m_Part2.m_pF[j][0];

		for (uint32_t i = 1; i < Cfg::n - 1; i++)
			mctx.pF0[j] += m_Part2.m_pF[j][i];

		mctx.pF0[j] = -mctx.pF0[j];
		mctx.pF0[j] += mctx.x;
	}


	{
		// A + B*x =?= Commitment(f, Za)
		{
			struct Commitment_1 :public CommitmentStd
			{
				const MyContext* m_p;
				virtual void get_At(Scalar::Native& x, uint32_t j, uint32_t i) override
				{
					m_p->get_F(x, j, i);
				}
			} c;
			c.m_p = &mctx;

			if (!c.IsValid(bc, m_Part1.m_A, m_Part1.m_B, mctx.x, m_Part2.m_zA))
				return false;
		}

		// D + C*x =?= Commitment(f*(x-f), Zc)
		{
			struct Commitment_2 :public CommitmentStd
			{
				const MyContext* m_p;
				virtual void get_At(Scalar::Native& x, uint32_t j, uint32_t i) override
				{
					Scalar::Native v;
					m_p->get_F(v, j, i);

					x = m_p->x;
					x *= v;

					v *= v;
					x -= v;
				}
			} c;
			c.m_p = &mctx;

			if (!c.IsValid(bc, m_Part1.m_D, m_Part1.m_C, mctx.x, m_Part2.m_zC))
				return false;
		}
	}

	// final calculation
	bc.EquationBegin();
	Scalar::Native kMulBalance = bc.m_Multiplier;
	Scalar::Native kMulBalanceChallenged = kMulBalance * x2;

	bc.EquationBegin();
	Scalar::Native kMulPG = bc.m_Multiplier;

	// G and Q
	Scalar::Native xPwr = 1U;
	for (uint32_t j = 0; j < Cfg::M; j++)
	{
		bc.m_Multiplier = -kMulPG;
		if (!bc.AddCasual(m_Part1.m_pG[j], xPwr)) // + G[j] * (-mulPG)
			return false;


		bc.m_Multiplier += kMulBalanceChallenged;
		if (!bc.AddCasual(m_Part1.m_pQ[j], xPwr)) // P[j] * (-mulPG + mulBalance * challenge)
			return false;

		xPwr *= mctx.x;
	}

	bc.m_Multiplier = kMulBalanceChallenged;

	xPwr = -xPwr;
	if (!bc.AddCasual(m_Part1.m_Output, xPwr))
		return false;

	bc.AddPrepared(InnerProduct::BatchContext::s_Idx_G, m_Part2.m_zR);
	bc.AddPrepared(InnerProduct::BatchContext::s_Idx_H, m_Part2.m_zV);

	if (!bc.AddCasual(m_Part1.m_NonceG, kMulBalance, true))
		return false;

	bc.m_Multiplier = kMulBalance;

	if (!bc.AddCasual(m_Part1.m_SpendPk, x3))
		return false;

	bc.AddPrepared(InnerProduct::BatchContext::s_Idx_G, m_Part2.m_ProofG);

	bc.m_Multiplier = kMulPG;

	// Commitments from CmList
	Scalar::Native kSer(Zero);

	for (uint32_t iPos = 0; iPos < Cfg::N; iPos++)
	{
		Point pt;
		if (!cmList.get_At(pt, iPos))
			break;

		Scalar::Native k = kMulPG;

		uint32_t ij = iPos;

		for (uint32_t j = 0; j < Cfg::M; j++)
		{
			mctx.get_F(xPwr, j, ij % Cfg::n);
			k *= xPwr;
			ij /= Cfg::n;
		}

		kSer += k;

		if (!bc.AddCasual(pt, k, true))
			return false;
	}

	SpendKey::ToSerial(xPwr, m_Part1.m_SpendPk);

	kSer = -kSer;
	kSer *= xPwr;
	bc.AddPreparedM(InnerProduct::BatchContext::s_Idx_J, kSer);

	// compare with target
	bc.m_Multiplier = -kMulPG;
	bc.AddPrepared(InnerProduct::BatchContext::s_Idx_G, m_Part2.m_zR);
	bc.AddPrepared(InnerProduct::BatchContext::s_Idx_H, m_Part2.m_zV);

	return true;
}


///////////////////////////
// Prover
void Prover::InitNonces(const uintBig& seed)
{
	NonceGenerator nonceGen("lel0");
	nonceGen << seed;

	nonceGen
		>> m_rA
		>> m_rB
		>> m_rC
		>> m_rD;

	for (uint32_t j = 0; j < Cfg::M; j++)
	{
		nonceGen
			>> m_Gamma[j]
			>> m_Ro[j]
			>> m_Tau[j];

		for (uint32_t i = 1; i < Cfg::n; i++)
		{
			nonceGen >> m_a[j][i];
			m_a[j][0] += -m_a[j][i];
		}
	}

	nonceGen
		>> m_rBalance;
}

void Prover::CalculateP()
{
	m_p[0][0] = 1U;

	uint32_t nPwr = 1;
	for (uint32_t j = 0; j < Cfg::M; j++)
	{
		uint32_t i0 = (m_Witness.V.m_L / nPwr) % Cfg::n;

		for (uint32_t i = Cfg::n; i--; )
		{
			bool bMatch = (i == i0);

			if (j + 1 < Cfg::M)
			{
				for (uint32_t t = nPwr; t--; )
					if (bMatch)
						m_p[j + 1][i * nPwr + t] = m_p[j][t];
					else
						m_p[j + 1][i * nPwr + t] = Zero;
			}

			for (uint32_t k = j; ; )
			{
				for (uint32_t t = nPwr; t--; )
				{
					if (i)
						m_p[k][i * nPwr + t] = m_p[k][t];
					m_p[k][i * nPwr + t] *= m_a[j][i];

					if (bMatch && k)
						m_p[k][i * nPwr + t] += m_p[k - 1][t];
				}

				if (!k--)
					break;
			}
		}

		nPwr *= Cfg::n;
	}
}

void Prover::ExtractABCD()
{
	CommitmentStd::MultiMacMy mm;

	{
		struct Commitment_A :public CommitmentStd
		{
			Prover* m_p;
			virtual void get_At(Scalar::Native& x, uint32_t j, uint32_t i) override
			{
				x = m_p->m_a[j][i];
			}
		} c;
		c.m_p = this;
		c.Calculate(m_Proof.m_Part1.m_A, mm, m_rA);
	}

	{
		struct Commitment_B :public CommitmentStd
		{
			uint32_t m_L_Reduced;
			virtual void get_At(Scalar::Native& x, uint32_t j, uint32_t i) override
			{
				assert(i < Cfg::n);

				if ((m_L_Reduced % Cfg::n) == i)
					x = 1U;
				else
					x = Zero;

				if (Cfg::n - 1 == i)
					m_L_Reduced /= Cfg::n;
			}
		} c;

		c.m_L_Reduced = m_Witness.V.m_L;
		c.Calculate(m_Proof.m_Part1.m_B, mm, m_rB);
	}

	{
		struct Commitment_C :public CommitmentStd
		{
			Prover* m_p;
			uint32_t m_L_Reduced;

			virtual void get_At(Scalar::Native& x, uint32_t j, uint32_t i) override
			{
				assert(i < Cfg::n);
				x = m_p->m_a[j][i];

				if ((m_L_Reduced % Cfg::n) == i)
					x = -x;

				if (Cfg::n - 1 == i)
					m_L_Reduced /= Cfg::n;
			}
		} c;

		c.m_p = this;
		c.m_L_Reduced = m_Witness.V.m_L;
		c.Calculate(m_Proof.m_Part1.m_C, mm, m_rC);
	}

	{
		struct Commitment_D :public CommitmentStd
		{
			Prover* m_p;
			virtual void get_At(Scalar::Native& x, uint32_t j, uint32_t i) override
			{
				x = m_p->m_a[j][i];
				x *= x;
				x = -x;
			}
		} c;

		c.m_p = this;
		c.Calculate(m_Proof.m_Part1.m_D, mm, m_rD);
	}
}

void Prover::ExtractGQ()
{
	const uint32_t nSizeNaggle = 128;
	MultiMac_WithBufs<nSizeNaggle, 1> mm;

	uint32_t iPos = 0;
	Point::Native pG[Cfg::M], comm, comm2;
	Scalar::Native s1;

	while (true)
	{
		for (mm.Reset(); static_cast<uint32_t>(mm.m_Casual) < nSizeNaggle; mm.m_Casual++)
		{
			Point pt;
			if (!m_List.get_At(pt, iPos + mm.m_Casual))
				break;
			if (!comm.Import(pt))
				break; //?!?

			mm.m_pCasual[mm.m_Casual].Init(comm);
		}

		bool bLast = (Cfg::N == iPos + mm.m_Casual) || (static_cast<uint32_t>(mm.m_Casual) < nSizeNaggle);

		mm.m_ppPrepared[mm.m_Prepared] = &Context::get().m_Ipp.J_;
		Scalar::Native& kSer = mm.m_pKPrep[mm.m_Prepared++];

		for (uint32_t k = 0; k < Cfg::M; k++)
		{
			kSer = Zero;

			for (uint32_t i = 0; i < static_cast<uint32_t>(mm.m_Casual); i++)
			{
				mm.m_pCasual[i].m_K = m_p[k][iPos + i];
				kSer += m_p[k][iPos + i];
			}

			kSer *= m_Serial;
			kSer = -kSer;

			mm.Calculate(comm);
			pG[k] += comm;
		}

		if (bLast)
			break;

		iPos += mm.m_Casual;
	}

	// add gammas, split G[] into G[] and Q[]
	for (uint32_t k = 0; k < Cfg::M; k++)
	{
		comm = Context::get().G * m_Gamma[k];

		comm2 = comm;
		comm2 += Context::get().G * m_Tau[k];
		comm2 += Context::get().H_Big * m_Ro[k];
		m_Proof.m_Part1.m_pQ[k] = comm2;

		comm = -comm;
		comm += pG[k];
		m_Proof.m_Part1.m_pG[k] = comm;
	}
}

void Prover::ExtractBlinded(Scalar& out, const Scalar::Native& sk, const Scalar::Native& challenge, const Scalar::Native& nonce)
{
	Scalar::Native val = sk;
	val *= challenge;
	val += nonce;
	out = val;
}

void Prover::ExtractPart2(Oracle& oracle)
{
	Scalar::Native x1, x2, x3;
	oracle >> x1;
	oracle >> x2; // balance proof challenge
	oracle >> x3; // spend proof challenge

	ExtractBlinded(m_Proof.m_Part2.m_zA, m_rB, x1, m_rA);
	ExtractBlinded(m_Proof.m_Part2.m_zC, m_rC, x1, m_rD);

	Scalar::Native zV(Zero), zR(Zero), xPwr(1U);

	Scalar::Native kBalance = Zero;

	for (uint32_t j = 0; j < Cfg::M; j++)
	{
		zV += m_Ro[j] * xPwr;
		zR += m_Tau[j] * xPwr;

		kBalance += m_Gamma[j] * xPwr;

		xPwr *= x1;
	}

	Scalar::Native dR = m_Witness.V.m_R - m_Witness.V.m_R_Output;
	kBalance += dR * xPwr;
	kBalance *= x2; // challenge
	kBalance += m_rBalance; // blinding

	kBalance += x3 * m_Witness.V.m_SpendSk;

	kBalance = -kBalance;
	m_Proof.m_Part2.m_ProofG = kBalance;

	zV = -zV;
	zV += Scalar::Native(m_Witness.V.m_V) * xPwr;
	m_Proof.m_Part2.m_zV = zV;

	zR = -zR;
	zR += m_Witness.V.m_R * xPwr;
	m_Proof.m_Part2.m_zR = zR;

	uint32_t nL_Reduced = m_Witness.V.m_L;

	for (uint32_t j = 0; j < Cfg::M; j++)
	{
		uint32_t i0 = nL_Reduced % Cfg::n;
		nL_Reduced /= Cfg::n;

		for (uint32_t i = 1; i < Cfg::n; i++)
		{
			xPwr = m_a[j][i];
			if (i == i0)
				xPwr += x1;

			m_Proof.m_Part2.m_pF[j][i - 1] = xPwr;
		}
	}
}

void Prover::Generate(const uintBig& seed, Oracle& oracle)
{
	m_Proof.m_Part1.m_SpendPk = Context::get().G * m_Witness.V.m_SpendSk;
	SpendKey::ToSerial(m_Serial, m_Proof.m_Part1.m_SpendPk);

	InitNonces(seed);
	ExtractABCD();
	CalculateP();
	ExtractGQ();

	m_Proof.m_Part1.m_Output = Commitment(m_Witness.V.m_R_Output, m_Witness.V.m_V);
	m_Proof.m_Part1.m_NonceG = Context::get().G * m_rBalance;

	m_Proof.m_Part1.Expose(oracle);

	ExtractPart2(oracle);
}



} // namespace beam
} // namespace Lelantus
