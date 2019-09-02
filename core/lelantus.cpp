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
	struct MultiMacMy
		:public MultiMac_WithBufs<1, Cfg::Max::nM + 1>
	{
		MultiMacMy(const Cfg& cfg)
		{
			assert(cfg.get_N());
			assert(cfg.M * cfg.n + 1 <= _countof(m_Bufs.m_pKPrep));

			static_assert(Cfg::Max::nM + 1 <= InnerProduct::nDim * 2);
			const MultiMac::Prepared* pP0 = Context::get().m_Ipp.m_pGen_[0];

			for (uint32_t j = 0; j < cfg.M; j++)
			{
				for (uint32_t i = 0; i < cfg.n; i++, m_Prepared++)
				{
					m_ppPrepared[m_Prepared] = pP0 + m_Prepared;
				}
			}

			m_ppPrepared[m_Prepared] = pP0 + m_Prepared;
			m_Prepared++;
		}
	};

	const Cfg* m_pCfg = nullptr;

	virtual void get_At(Scalar::Native&, uint32_t j, uint32_t i) = 0;

	void FillEquation(MultiMac& mm, const Scalar::Native& blinding, const Scalar::Native* pMultiplier = nullptr)
	{
		uint32_t iIdx = 0;
		Scalar::Native k;

		for (uint32_t j = 0; j < m_pCfg->M; j++)
		{
			for (uint32_t i = 0; i < m_pCfg->n; i++, iIdx++)
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

void CmList::Import(MultiMac& mm, uint32_t iPos, uint32_t nCount)
{
	Point::Native comm;

	for (mm.Reset(); static_cast<uint32_t>(mm.m_Casual) < nCount; mm.m_Casual++)
	{
		Point::Storage pt_s;
		if (!get_At(pt_s, iPos + mm.m_Casual))
			break;

		comm.Import(pt_s, false);
		mm.m_pCasual[mm.m_Casual].Init(comm);
	}
}

void CmList::Calculate(Point::Native& res, uint32_t iPos, uint32_t nCount, const Scalar::Native* pKs)
{
	Mode::Scope scope(Mode::Fast);

	const uint32_t nSizeNaggle = 128;
	MultiMac_WithBufs<nSizeNaggle, 1> mm;

	Point::Native comm;

	while (true)
	{
		Import(mm, iPos, std::min(nSizeNaggle, nCount));
		mm.m_pKCasual = Cast::NotConst(pKs + iPos);

		mm.Calculate(comm);
		res += comm;

		iPos += mm.m_Casual;
		nCount -= mm.m_Casual;

		if (!nCount || (static_cast<uint32_t>(mm.m_Casual) < nSizeNaggle))
			break;
	}
}

///////////////////////////
// Cfg
uint32_t Cfg::get_N() const
{
	// typical n is 2 or 4
	if ((n < 2) || (n > Max::n))
		return false;

	if (!M || (M > Max::M))
		return false;

	static_assert(Max::M * Max::n <= static_cast<uint32_t>(-1), ""); // no chance of overflow
	if (M * n > Max::nM)
		return false;

	uint64_t ret = 1;
	for (uint32_t i = 0; i < M; i++)
	{
		ret *= n;
		if (ret > Max::N)
			return 0;
	}

	static_assert(Max::N <= static_cast<uint32_t>(-1), "");
	return static_cast<uint32_t>(ret);
}

uint32_t Cfg::get_F() const
{
	return M * (n - 1);
}

///////////////////////////
// Proof
void Proof::Part1::Expose(Oracle& oracle) const
{
	oracle
		<< m_SpendPk
		<< m_NonceG
		<< m_A
		<< m_B
		<< m_C
		<< m_D;

	for (uint32_t k = 0; k < m_vGQ.size(); k++)
	{
		const GQ& x = m_vGQ[k];
		oracle
			<< x.m_G
			<< x.m_Q;
	}
}

bool Proof::IsValid(InnerProduct::BatchContext& bc, Oracle& oracle, const Output& outp, Scalar::Native* pKs) const
{
	const uint32_t N = m_Cfg.get_N();
	if (!N)
		return false;

	if (m_Part1.m_vGQ.size() != m_Cfg.M)
		return false;

	if (m_Part2.m_vF.size() != m_Cfg.get_F())
		return false;

	Mode::Scope scope(Mode::Fast);

	struct MyContext
	{
		const Scalar* m_pF1;
		uint32_t m_Pitch;

		Scalar::Native pF0[Cfg::Max::M];
		Scalar::Native x;

		void get_F(Scalar::Native& out, uint32_t j, uint32_t i) const
		{
			out = i ?
				m_pF1[j * m_Pitch + i] :
				pF0[j];
		}

	} mctx;
	mctx.m_pF1 = &m_Part2.m_vF.front() - 1;
	mctx.m_Pitch = m_Cfg.n - 1;

	m_Part1.Expose(oracle);
	oracle << outp.m_Commitment;
	oracle >> mctx.x;

	Scalar::Native x2, x3;
	oracle >> x2; // balance proof challenge
	oracle >> x3; // spend proof challenge

	// recover pF0
	auto itF = m_Part2.m_vF.begin();
	for (uint32_t j = 0; j < m_Cfg.M; j++)
	{
		mctx.pF0[j] = *itF++;

		for (uint32_t i = 1; i < m_Cfg.n - 1; i++)
			mctx.pF0[j] += *itF++;

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
			c.m_pCfg = &m_Cfg;

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
			c.m_pCfg = &m_Cfg;

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
	for (uint32_t j = 0; j < m_Cfg.M; j++)
	{
		const Part1::GQ& x = m_Part1.m_vGQ[j];
		bc.m_Multiplier = -kMulPG;
		if (!bc.AddCasual(x.m_G, xPwr)) // + G[j] * (-mulPG)
			return false;


		bc.m_Multiplier += kMulBalanceChallenged;
		if (!bc.AddCasual(x.m_Q, xPwr)) // P[j] * (-mulPG + mulBalance * challenge)
			return false;

		xPwr *= mctx.x;
	}

	bc.m_Multiplier = kMulBalanceChallenged;

	xPwr = -xPwr;
	bc.AddCasual(outp.m_Pt, xPwr);

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

	Point::Native pt;
	for (uint32_t iPos = 0; iPos < N; iPos++)
	{
		Scalar::Native k = kMulPG;

		uint32_t ij = iPos;

		for (uint32_t j = 0; j < m_Cfg.M; j++)
		{
			mctx.get_F(xPwr, j, ij % m_Cfg.n);
			k *= xPwr;
			ij /= m_Cfg.n;
		}

		kSer += k;

		pKs[iPos] += k;
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
		>> m_vBuf[Idx::rA]
		>> m_vBuf[Idx::rB]
		>> m_vBuf[Idx::rC]
		>> m_vBuf[Idx::rD];

	Scalar::Native* pA = m_a;
	for (uint32_t j = 0; j < m_Proof.m_Cfg.M; j++)
	{
		nonceGen
			>> m_Gamma[j]
			>> m_Ro[j]
			>> m_Tau[j];

		for (uint32_t i = 1; i < m_Proof.m_Cfg.n; i++)
		{
			nonceGen >> pA[i];
			pA[0] += -pA[i];
		}

		pA += m_Proof.m_Cfg.n;
	}

	nonceGen
		>> m_vBuf[Idx::rBalance];
}

void Prover::CalculateP()
{
	m_p[0] = 1U;

	const uint32_t N = m_Proof.m_Cfg.get_N();
	assert(N);

	Scalar::Native* pA = m_a;
	Scalar::Native* pP = m_p;
	uint32_t nPwr = 1;
	for (uint32_t j = 0; j < m_Proof.m_Cfg.M; j++)
	{
		uint32_t i0 = (m_Witness.V.m_L / nPwr) % m_Proof.m_Cfg.n;

		pP += N;

		for (uint32_t i = m_Proof.m_Cfg.n; i--; )
		{
			bool bMatch = (i == i0);

			if (j + 1 < m_Proof.m_Cfg.M)
			{
				for (uint32_t t = nPwr; t--; )
					if (bMatch)
						pP[i * nPwr + t] = pP[static_cast<int32_t>(t - N)];
					else
						pP[i * nPwr + t] = Zero;
			}

			Scalar::Native* pP0 = pP;

			for (uint32_t k = j; ; )
			{
				pP0 -= N;

				for (uint32_t t = nPwr; t--; )
				{
					if (i)
						pP0[i * nPwr + t] = pP0[t];
					pP0[i * nPwr + t] *= pA[i];

					if (bMatch && k)
						pP0[i * nPwr + t] += pP0[static_cast<int32_t>(t - N)];
				}

				if (!k--)
					break;
			}
		}

		pA += m_Proof.m_Cfg.n;
		nPwr *= m_Proof.m_Cfg.n;
	}
}

void Prover::ExtractABCD()
{
	CommitmentStd::MultiMacMy mm(m_Proof.m_Cfg);

	{
		struct Commitment_A :public CommitmentStd
		{
			Prover* m_p;
			virtual void get_At(Scalar::Native& x, uint32_t j, uint32_t i) override
			{
				x = m_p->m_a[j * m_pCfg->n + i];
			}
		} c;
		c.m_p = this;
		c.m_pCfg = &m_Proof.m_Cfg;
		c.Calculate(m_Proof.m_Part1.m_A, mm, m_vBuf[Idx::rA]);
	}

	{
		struct Commitment_B :public CommitmentStd
		{
			uint32_t m_L_Reduced;
			virtual void get_At(Scalar::Native& x, uint32_t j, uint32_t i) override
			{
				assert(i < m_pCfg->n);

				if ((m_L_Reduced % m_pCfg->n) == i)
					x = 1U;
				else
					x = Zero;

				if (m_pCfg->n - 1 == i)
					m_L_Reduced /= m_pCfg->n;
			}
		} c;

		c.m_L_Reduced = m_Witness.V.m_L;
		c.m_pCfg = &m_Proof.m_Cfg;
		c.Calculate(m_Proof.m_Part1.m_B, mm, m_vBuf[Idx::rB]);
	}

	{
		struct Commitment_C :public CommitmentStd
		{
			Prover* m_p;
			uint32_t m_L_Reduced;

			virtual void get_At(Scalar::Native& x, uint32_t j, uint32_t i) override
			{
				assert(i < m_pCfg->n);
				x = m_p->m_a[j * m_pCfg->n + i];

				if ((m_L_Reduced % m_pCfg->n) == i)
					x = -x;

				if (m_pCfg->n - 1 == i)
					m_L_Reduced /= m_pCfg->n;
			}
		} c;

		c.m_p = this;
		c.m_pCfg = &m_Proof.m_Cfg;
		c.m_L_Reduced = m_Witness.V.m_L;
		c.Calculate(m_Proof.m_Part1.m_C, mm, m_vBuf[Idx::rC]);
	}

	{
		struct Commitment_D :public CommitmentStd
		{
			Prover* m_p;
			virtual void get_At(Scalar::Native& x, uint32_t j, uint32_t i) override
			{
				x = m_p->m_a[j * m_pCfg->n + i];
				x *= x;
				x = -x;
			}
		} c;

		c.m_p = this;
		c.m_pCfg = &m_Proof.m_Cfg;
		c.Calculate(m_Proof.m_Part1.m_D, mm, m_vBuf[Idx::rD]);
	}
}

void Prover::ExtractGQ()
{
	const uint32_t nSizeNaggle = 128;
	MultiMac_WithBufs<nSizeNaggle, 1> mm;

	uint32_t iPos = 0;
	Point::Native pG[Cfg::Max::M], comm, comm2;
	Scalar::Native s1;

	const uint32_t N = m_Proof.m_Cfg.get_N();
	assert(N);

	while (true)
	{
		m_List.Import(mm, iPos, std::min(nSizeNaggle, N - iPos));

		mm.m_ppPrepared[mm.m_Prepared] = &Context::get().m_Ipp.J_;
		Scalar::Native& kSer = mm.m_pKPrep[mm.m_Prepared++];

		Scalar::Native* pP = m_p;
		for (uint32_t k = 0; k < m_Proof.m_Cfg.M; k++)
		{
			kSer = Zero;

			for (uint32_t i = 0; i < static_cast<uint32_t>(mm.m_Casual); i++)
				kSer += pP[iPos + i];

			kSer *= m_vBuf[Idx::Serial];
			kSer = -kSer;

			mm.m_pKCasual = pP + iPos;
			mm.Calculate(comm);
			pG[k] += comm;

			pP += N;
		}

		iPos += mm.m_Casual;

		if ((N == iPos) || (static_cast<uint32_t>(mm.m_Casual) < nSizeNaggle))
			break;
	}

	// add gammas, split G[] into G[] and Q[]
	for (uint32_t k = 0; k < m_Proof.m_Cfg.M; k++)
	{
		Proof::Part1::GQ& res = m_Proof.m_Part1.m_vGQ[k];
		comm = Context::get().G * m_Gamma[k];

		comm2 = comm;
		comm2 += Context::get().G * m_Tau[k];
		comm2 += Context::get().H_Big * m_Ro[k];
		res.m_Q = comm2;

		comm = -comm;
		comm += pG[k];
		res.m_G = comm;
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

	ExtractBlinded(m_Proof.m_Part2.m_zA, m_vBuf[Idx::rB], x1, m_vBuf[Idx::rA]);
	ExtractBlinded(m_Proof.m_Part2.m_zC, m_vBuf[Idx::rC], x1, m_vBuf[Idx::rD]);

	Scalar::Native zV(Zero), zR(Zero), xPwr(1U);

	Scalar::Native kBalance = Zero;

	for (uint32_t j = 0; j < m_Proof.m_Cfg.M; j++)
	{
		zV += m_Ro[j] * xPwr;
		zR += m_Tau[j] * xPwr;

		kBalance += m_Gamma[j] * xPwr;

		xPwr *= x1;
	}

	Scalar::Native dR = m_Witness.V.m_R - m_Witness.V.m_R_Output;
	kBalance += dR * xPwr;
	kBalance *= x2; // challenge
	kBalance += m_vBuf[Idx::rBalance]; // blinding

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

	Scalar::Native* pA = m_a;
	auto itF = m_Proof.m_Part2.m_vF.begin();
	for (uint32_t j = 0; j < m_Proof.m_Cfg.M; j++)
	{
		uint32_t i0 = nL_Reduced % m_Proof.m_Cfg.n;
		nL_Reduced /= m_Proof.m_Cfg.n;

		for (uint32_t i = 1; i < m_Proof.m_Cfg.n; i++)
		{
			xPwr = pA[i];
			if (i == i0)
				xPwr += x1;

			*itF++ = xPwr;
		}

		pA += m_Proof.m_Cfg.n;
	}
}

void Prover::Generate(Proof::Output& outp, const uintBig& seed, Oracle& oracle)
{
	const uint32_t N = m_Proof.m_Cfg.get_N();
	assert(N);

	m_vBuf.reset(new Scalar::Native[Idx::CountFixed + m_Proof.m_Cfg.M * (3 + m_Proof.m_Cfg.n + N)]);

	m_Proof.m_Part1.m_vGQ.resize(m_Proof.m_Cfg.M);
	m_Proof.m_Part2.m_vF.resize(m_Proof.m_Cfg.M * (m_Proof.m_Cfg.n - 1));

	m_Gamma = m_vBuf.get() + Idx::CountFixed;
	m_Ro = m_Gamma + m_Proof.m_Cfg.M;
	m_Tau = m_Ro + m_Proof.m_Cfg.M;
	m_a = m_Tau + m_Proof.m_Cfg.M;
	m_p = m_a + m_Proof.m_Cfg.M * m_Proof.m_Cfg.n;

	m_Proof.m_Part1.m_SpendPk = Context::get().G * m_Witness.V.m_SpendSk;
	SpendKey::ToSerial(m_vBuf[Idx::Serial], m_Proof.m_Part1.m_SpendPk);

	InitNonces(seed);
	ExtractABCD();
	CalculateP();
	ExtractGQ();

	outp.m_Pt = Commitment(m_Witness.V.m_R_Output, m_Witness.V.m_V);
	outp.m_Commitment = outp.m_Pt;

	m_Proof.m_Part1.m_NonceG = Context::get().G * m_vBuf[Idx::rBalance];

	m_Proof.m_Part1.Expose(oracle);
	oracle << outp.m_Commitment;

	ExtractPart2(oracle);
}



} // namespace beam
} // namespace Lelantus
