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

using namespace ECC;

namespace Sigma {

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
		return 0;

	if (!M || (M > Max::M))
		return 0;

	static_assert(Max::M * Max::n <= static_cast<uint32_t>(-1), ""); // no chance of overflow
	if (M * n > Max::nM)
		return 0;

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
		<< m_A
		<< m_B
		<< m_C
		<< m_D;

	for (uint32_t k = 0; k < m_vG.size(); k++)
		oracle << m_vG[k];
}

bool Proof::IsValid(InnerProduct::BatchContext& bc, Oracle& oracle, Scalar::Native* pKs, Scalar::Native& kBias) const
{
	const uint32_t N = m_Cfg.get_N();
	if (!N)
		return false;

	if (m_Part1.m_vG.size() != m_Cfg.M)
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

		Scalar::Native m_kBias;
		Scalar::Native* m_pKs;
		uint32_t m_n;

		void FillKs(const Scalar::Native& k, uint32_t iBit)
		{
			if (!iBit--)
			{
				*m_pKs++ += k;
				m_kBias += k;
				return;
			}

			Scalar::Native k2;

			for (uint32_t i = 0; i < m_n; i++)
			{
				get_F(k2, iBit, i);
				k2 *= k;

				FillKs(k2, iBit);
			}
		}

	} mctx;
	mctx.m_pF1 = &m_Part2.m_vF.front() - 1;
	mctx.m_Pitch = m_Cfg.n - 1;

	m_Part1.Expose(oracle);
	oracle >> mctx.x;

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

	// G
	Scalar::Native xPwr = -bc.m_Multiplier;
	for (uint32_t j = 0; j < m_Cfg.M; j++)
	{
		if (!bc.AddCasual(m_Part1.m_vG[j], xPwr, true)) // - G[j] * x^j
			return false;

		xPwr *= mctx.x;
	}

	// Commitments from CmList
	mctx.m_pKs = pKs;
	mctx.m_n = m_Cfg.n;
	mctx.m_kBias = Zero;

	mctx.FillKs(bc.m_Multiplier, m_Cfg.M);

	kBias = -mctx.m_kBias;

	// compare with target
	bc.m_Multiplier = -bc.m_Multiplier;
	bc.AddPrepared(InnerProduct::BatchContext::s_Idx_G, m_Part2.m_zR);

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
		nonceGen >> m_Tau[j];

		for (uint32_t i = 1; i < m_Proof.m_Cfg.n; i++)
		{
			nonceGen >> pA[i];
			pA[0] += -pA[i];
		}

		pA += m_Proof.m_Cfg.n;
	}
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

void Prover::ExtractG(const Point::Native& ptBias)
{
	const uint32_t nSizeNaggle = 128;
	MultiMac_WithBufs<nSizeNaggle, 1> mm;

	uint32_t iPos = 0;
	Point::Native pG[Cfg::Max::M], comm, comm2;

	const uint32_t N = m_Proof.m_Cfg.get_N();
	assert(N);

	Scalar::Native pBias[Cfg::Max::M];
	for (uint32_t k = 0; k < m_Proof.m_Cfg.M; k++)
		pBias[k] = Zero;

	while (true)
	{
		m_List.Import(mm, iPos, std::min(nSizeNaggle, N - iPos));
		mm.m_ReuseFlag = MultiMac::Reuse::Generate;

		Scalar::Native* pP = m_p;
		for (uint32_t k = 0; k < m_Proof.m_Cfg.M; k++)
		{
			mm.m_pKCasual = pP + iPos;

			for (uint32_t i = 0; i < static_cast<uint32_t>(mm.m_Casual); i++)
				pBias[k] += pP[iPos + i];

			mm.Calculate(comm);
			pG[k] += comm;

			mm.m_ReuseFlag = MultiMac::Reuse::UseGenerated;

			pP += N;
		}

		iPos += mm.m_Casual;

		if ((N == iPos) || (static_cast<uint32_t>(mm.m_Casual) < nSizeNaggle))
			break;
	}

	mm.Reset();
	mm.m_ppPrepared[0] = &Context::get().m_Ipp.G_;
	mm.m_pCasual[0].Init(-ptBias);
	mm.m_Prepared = 1;
	mm.m_Casual = 1;

	for (uint32_t k = 0; k < m_Proof.m_Cfg.M; k++)
	{
		mm.m_pKPrep[0] = m_Tau[k]; // don't set it by pointer, the calculation in secure mode overwrites it!
		mm.m_pKCasual = pBias + k;
		mm.Calculate(comm);

		comm += pG[k];

		m_Proof.m_Part1.m_vG[k] = comm;
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
	Scalar::Native x1;
	oracle >> x1;

	ExtractBlinded(m_Proof.m_Part2.m_zA, m_vBuf[Idx::rB], x1, m_vBuf[Idx::rA]);
	ExtractBlinded(m_Proof.m_Part2.m_zC, m_vBuf[Idx::rC], x1, m_vBuf[Idx::rD]);

	Scalar::Native zR(Zero), xPwr(1U);

	for (uint32_t j = 0; j < m_Proof.m_Cfg.M; j++)
	{
		zR += m_Tau[j] * xPwr;
		xPwr *= x1;
	}

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

void Prover::Generate(const uintBig& seed, Oracle& oracle, const ECC::Point::Native& ptBias)
{
	// Since this is a heavy proof, do it in 'fast' mode. Use 'secure' mode only for the most sensitive part - the SpendSk
	Mode::Scope scope(Mode::Fast);

	const uint32_t N = m_Proof.m_Cfg.get_N();
	assert(N);

	m_vBuf.reset(new Scalar::Native[Idx::count + m_Proof.m_Cfg.M * (1 + m_Proof.m_Cfg.n + N)]);

	m_Proof.m_Part1.m_vG.resize(m_Proof.m_Cfg.M);
	m_Proof.m_Part2.m_vF.resize(m_Proof.m_Cfg.M * (m_Proof.m_Cfg.n - 1));

	m_Tau = m_vBuf.get() + Idx::count;
	m_a = m_Tau + m_Proof.m_Cfg.M;
	m_p = m_a + m_Proof.m_Cfg.M * m_Proof.m_Cfg.n;

	InitNonces(seed);
	ExtractABCD();
	CalculateP();
	ExtractG(ptBias);

	m_Proof.m_Part1.Expose(oracle);
	ExtractPart2(oracle);
}



} // namespace Sigma

namespace Lelantus {

void SpendKey::ToSerial(Scalar::Native& serial, const Point& pk)
{
	Oracle()
		<< "L.Spend"
		<< pk
		>> serial;
}

void Proof::Expose0(ECC::Oracle& oracle, Hash::Value& hv) const
{
	oracle
		<< m_Commitment
		<< m_SpendPk
		>> hv;
}

bool Proof::IsValid(ECC::InnerProduct::BatchContext& bc, ECC::Oracle& oracle, ECC::Scalar::Native* pKs) const
{
	Mode::Scope scope(Mode::Fast);

	Point::Native pN[2];
	if (!pN[0].Import(m_Commitment) ||
		!pN[1].Import(m_SpendPk))
		return false;

	{
		Hash::Value hv;
		Expose0(oracle, hv);

		InnerProduct::BatchContext::Scope scope2(bc);

		if (!m_Signature.IsValid(Context::get().m_Sig.m_CfgGH2, hv, m_Signature.m_pK, pN))
			return false;
	}

	Scalar::Native kBias, kSer;
	if (!Sigma::Proof::IsValid(bc, oracle, pKs, kBias))
		return false;

	bc.AddCasual(pN[0], kBias, true);

	SpendKey::ToSerial(kSer, m_SpendPk);
	kSer *= kBias;
	bc.AddPreparedM(InnerProduct::BatchContext::s_Idx_J, kSer);

	return true;
}

void Prover::Generate(const ECC::uintBig& seed, ECC::Oracle& oracle)
{
	Point::Native ptBias = Commitment(m_Witness.V.m_R_Output, m_Witness.V.m_V);
	m_Proof.m_Commitment = ptBias;
	m_Proof.m_SpendPk = Context::get().G * m_Witness.V.m_SpendSk;

	Hash::Value hv;
	m_Proof.Expose0(oracle, hv);

	Scalar::Native pSk[4], pRes[2];

	pSk[0] = m_Witness.V.m_R_Output;
	pSk[1] = m_Witness.V.m_V;
	pSk[2] = m_Witness.V.m_SpendSk;
	assert(pSk[3] == Zero);

	m_Proof.m_Signature.Sign(Context::get().m_Sig.m_CfgGH2, hv, m_Proof.m_Signature.m_pK, pSk, pRes);

	Scalar::Native kSer;
	SpendKey::ToSerial(kSer, m_Proof.m_SpendPk);
	ptBias += Context::get().J * kSer;

	Sigma::Prover spr(m_List, m_Proof);
	spr.m_Witness.V.m_L = m_Witness.V.m_L;
	spr.m_Witness.V.m_R = m_Witness.V.m_R;
	spr.m_Witness.V.m_R -= m_Witness.V.m_R_Output;

	spr.Generate(seed, oracle, ptBias);
}


} // namespace Lelantus
} // namespace beam
