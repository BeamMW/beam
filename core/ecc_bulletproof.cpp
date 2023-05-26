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

#include "common.h"
#include "ecc_native.h"

namespace ECC {

	/////////////////////
	// InnerProduct

	thread_local InnerProduct::BatchContext* InnerProduct::BatchContext::s_pInstance = NULL;

	InnerProduct::BatchContext::BatchContext(uint32_t nCasualTotal)
		:m_CasualTotal(nCasualTotal)
		,m_bDirty(false)
	{
		assert(nCasualTotal);
		m_Multiplier = Zero;

		m_ppPrepared = m_Bufs.m_ppPrepared;
		m_pKPrep = m_Bufs.m_pKPrep;
		m_pWnafPrepared = m_Bufs.m_pWnafPrepared;

		for (uint32_t j = 0; j < 2; j++)
			for (uint32_t i = 0; i < InnerProduct::nDim; i++)
				m_ppPrepared[i + j * InnerProduct::nDim] = &Context::get().m_Ipp.m_pGen_[j][i];

		m_ppPrepared[s_Idx_GenDot] = &Context::get().m_Ipp.m_GenDot_;
		m_ppPrepared[s_Idx_Aux2] = &Context::get().m_Ipp.m_Aux2_;
		m_ppPrepared[s_Idx_G] = &Context::get().m_Ipp.G_;
		m_ppPrepared[s_Idx_H] = &Context::get().m_Ipp.H_;
		m_ppPrepared[s_Idx_J] = &Context::get().m_Ipp.J_;

		m_Prepared = s_CountPrepared;
	}

	void InnerProduct::BatchContext::Calculate()
	{
		Point::Native res;
		Mode::Scope scope(Mode::Fast);
		MultiMac::Calculate(res);

		m_Sum += res;
	}

	bool InnerProduct::BatchContext::AddCasual(const Point& p, const Scalar::Native& k, bool bPremultiplied /* = false */)
	{
		Point::Native pt;
		if (!pt.Import(p))
			return false;

		AddCasual(pt, k, bPremultiplied);
		return true;
	}

	void InnerProduct::BatchContext::AddCasual(const Point::Native& pt, const Scalar::Native& k, bool bPremultiplied /* = false */)
	{
		if (uint32_t(m_Casual) == m_CasualTotal)
		{
			assert(s_CountPrepared == m_Prepared);
			m_Prepared = 0; // don't count them now
			Calculate();

			m_Casual = 0;
			m_Prepared = s_CountPrepared;
		}

		m_pKCasual[m_Casual] = k;
		if (!bPremultiplied)
			m_pKCasual[m_Casual] *= m_Multiplier;

		m_pCasual[m_Casual++].Init(pt);

	}

	void InnerProduct::BatchContext::AddPrepared(uint32_t i, const Scalar::Native& k)
	{
		AddPreparedM(i, k * m_Multiplier);
	}

	void InnerProduct::BatchContext::AddPreparedM(uint32_t i, const Scalar::Native& k)
	{
		assert(i < s_CountPrepared);
		m_Bufs.m_pKPrep[i] += k;
	}

	void InnerProduct::BatchContext::Reset()
	{
		m_bDirty = false;
	}

	bool InnerProduct::BatchContext::Flush()
	{
		if (!m_bDirty)
			return true;
		m_bDirty = false;

		Calculate();
		return (m_Sum == Zero);
	}

	void InnerProduct::BatchContext::EquationBegin()
	{
		if (!m_bDirty)
		{
			m_bDirty = true;

			m_Sum = Zero;
			m_Casual = 0;
			ZeroObjectUnchecked(m_Bufs.m_pKPrep);
		}

		// mutate multiplier
		if (m_Multiplier == Zero)
			m_Multiplier.GenRandomNnz();
		else
			Oracle() << m_Multiplier >> m_Multiplier;
	}

	void InnerProduct::Modifier::Channel::SetPwr(const Scalar::Native& x)
	{
		m_pV[0] = 1U;
		for (uint32_t i = 1; i < nDim; i++)
			m_pV[i] = m_pV[i - 1] * x;
	}

	void InnerProduct::Modifier::Set(Scalar::Native& dst, const Scalar::Native& src, int i, int j) const
	{
		if (m_ppC[j])
			dst = src * m_ppC[j]->m_pV[i];
		else
			dst = src;
	}

	struct InnerProduct::CalculatorBase
	{
		Scalar::Native m_pVal[2][nDim >> 1];
		const Scalar::Native* m_ppSrc[2];

		struct XSet
		{
			Scalar::Native m_Val[nCycles];
		};

		struct ChallengeSet {
			Scalar::Native m_DotMultiplier;
			XSet m_pX[2];
		};

		ChallengeSet m_Cs;

		uint32_t m_iCycle;
		uint32_t m_n;

		void CycleStart(Oracle& oracle)
		{
			m_n = nDim >> (m_iCycle + 1);

			oracle >> m_Cs.m_pX[0].m_Val[m_iCycle];
			m_Cs.m_pX[1].m_Val[m_iCycle].SetInv(m_Cs.m_pX[0].m_Val[m_iCycle]);
		}

		void CycleExpose(Oracle& oracle, const InnerProduct& ip)
		{
			for (int j = 0; j < 2; j++)
				oracle << ip.m_pLR[m_iCycle][j];
		}

		void CycleEnd()
		{
			if (!m_iCycle)
			{
				for (int j = 0; j < 2; j++)
					m_ppSrc[j] = m_pVal[j];
			}
		}

		void CondenseBase();
	};

	void InnerProduct::CalculatorBase::CondenseBase()
	{
		for (int j = 0; j < 2; j++)
			for (uint32_t i = 0; i < m_n; i++)
			{
				// dst and src need not to be distinct
				m_pVal[j][i] = m_ppSrc[j][i] * m_Cs.m_pX[j].m_Val[m_iCycle];
				m_pVal[j][i] += m_ppSrc[j][m_n + i] * m_Cs.m_pX[!j].m_Val[m_iCycle];
			}
	}

	struct InnerProduct::Calculator
		:public CalculatorBase
	{

		struct Aggregator
		{
			MultiMac& m_Mm;
			const XSet* m_pX[2];
			const Modifier& m_Mod;
			const Calculator* m_pCalc; // set if source are already condensed points
			InnerProduct::BatchContext* m_pBatchCtx;
			const int m_j;
			const unsigned int m_iCycleTrg;

			Aggregator(MultiMac& mm, const XSet* pX, const XSet* pXInv, const Modifier& mod, int j, unsigned int iCycleTrg)
				:m_Mm(mm)
				,m_Mod(mod)
				,m_pCalc(NULL)
				,m_pBatchCtx(NULL)
				,m_j(j)
				,m_iCycleTrg(iCycleTrg)				
			{
				m_pX[0] = pX;
				m_pX[1] = pXInv;
			}

			void Proceed(uint32_t iPos, uint32_t iCycle, const Scalar::Native& k);
			void ProceedRec(uint32_t iPos, uint32_t iCycle, const Scalar::Native& k, uint32_t j);
		};

		static const uint32_t s_iCycle0 = 2; // condense source generators into points (after 3 iterations, 8 points)

		Point::Native m_pGen[2][nDim >> (1 + s_iCycle0)];

		const Modifier& m_Mod;

		MultiMac_WithBufs<(nDim >> (s_iCycle0 + 1)), nDim * 2> m_Mm;

		uint32_t m_GenOrder;

		void Condense();
		void ExtractLR(int j);

		Calculator(const Modifier& mod) :m_Mod(mod) {}
	};

	void InnerProduct::Calculator::Condense()
	{
		// Vectors
		CondenseBase();

		// Points
		switch (m_iCycle)
		{
		case s_iCycle0:
			// further compression points (casual)
			// Currently according to benchmarks - not necessary
			break;

		case nCycles - 1: // last iteration - no need to condense points
		default:
			return;
		}

		for (int j = 0; j < 2; j++)
			for (uint32_t i = 0; i < m_n; i++)
			{
				m_Mm.Reset();

				Point::Native& g0 = m_pGen[j][i];

				Aggregator aggr(m_Mm, &m_Cs.m_pX[0], &m_Cs.m_pX[1], m_Mod, j, nCycles - m_iCycle - 1);

				if (m_iCycle > s_iCycle0)
					aggr.m_pCalc = this;

				aggr.Proceed(i, m_GenOrder, 1U);

				m_Mm.Calculate(g0);
			}

		m_GenOrder = nCycles - m_iCycle - 1;
	}

	void InnerProduct::Calculator::ExtractLR(int j)
	{
		m_Mm.Reset();

		// Cross-term
		Scalar::Native& crossTrm = m_Mm.m_pKPrep[m_Mm.m_Prepared];
		m_Mm.m_ppPrepared[m_Mm.m_Prepared++] = &Context::get().m_Ipp.m_GenDot_;

		crossTrm = Zero;

		for (uint32_t i = 0; i < m_n; i++)
			crossTrm += m_ppSrc[j][i] * m_ppSrc[!j][m_n + i];

		crossTrm *= m_Cs.m_DotMultiplier;

		// other
		for (int jSrc = 0; jSrc < 2; jSrc++)
		{
			uint32_t off0 = (jSrc == j) ? 0 : m_n;
			uint32_t off1 = (jSrc == j) ? m_n : 0;

			for (uint32_t i = 0; i < m_n; i++)
			{
				const Scalar::Native& v = m_ppSrc[jSrc][i + off0];

				Aggregator aggr(m_Mm, &m_Cs.m_pX[0], &m_Cs.m_pX[1], m_Mod, jSrc, nCycles - m_iCycle);

				if (m_iCycle > s_iCycle0)
					aggr.m_pCalc = this;

				aggr.Proceed(i + off1, m_GenOrder, v);
			}
		}
	}

	void InnerProduct::Calculator::Aggregator::ProceedRec(uint32_t iPos, uint32_t iCycle, const Scalar::Native& k, uint32_t j)
	{
		if (m_pX[j])
		{
			Scalar::Native k0 = k;
			k0 *= m_pX[j]->m_Val[nCycles - iCycle];

			Proceed(iPos, iCycle - 1, k0);
		}
		else
			Proceed(iPos, iCycle - 1, k); // in batch mode all inverses are already multiplied
	}

	void InnerProduct::Calculator::Aggregator::Proceed(uint32_t iPos, uint32_t iCycle, const Scalar::Native& k)
	{
		if (iCycle != m_iCycleTrg)
		{
			assert(iCycle <= nCycles);
			ProceedRec(iPos, iCycle, k, !m_j);

			uint32_t nStep = 1 << (iCycle - 1);
			ProceedRec(iPos + nStep, iCycle, k, m_j);

		} else
		{
			if (m_pCalc)
			{
				assert(iPos < _countof(m_pCalc->m_pGen[m_j]));
				m_Mm.m_pKCasual[m_Mm.m_Casual] = k;
				m_Mm.m_pCasual[m_Mm.m_Casual++].Init(m_pCalc->m_pGen[m_j][iPos]);
			}
			else
			{
				assert(iPos < nDim);

				if (m_pBatchCtx)
				{
					Scalar::Native k2;
					m_Mod.Set(k2, k, iPos, m_j);

					m_pBatchCtx->AddPreparedM(iPos + m_j * InnerProduct::nDim, k2);
				}
				else
				{
					m_Mod.Set(m_Mm.m_pKPrep[m_Mm.m_Prepared], k, iPos, m_j);
					m_Mm.m_ppPrepared[m_Mm.m_Prepared++] = &Context::get().m_Ipp.m_pGen_[m_j][iPos];
				}
			}
		}
	}

	void InnerProduct::get_Dot(Scalar::Native& res, const Scalar::Native* pA, const Scalar::Native* pB)
	{
		static_assert(nDim, "");
		res = pA[0];
		res *= pB[0];

		Scalar::Native tmp;

		for (uint32_t i = 1; i < nDim; i++)
		{
			tmp = pA[i];
			tmp *= pB[i];
			res += tmp;
		}
	}

	void InnerProduct::Create(Point::Native& commAB, const Scalar::Native& dotAB, const Scalar::Native* pA, const Scalar::Native* pB, const Modifier& mod)
	{
		Oracle oracle;
		Create(oracle, &commAB, dotAB, pA, pB, mod);
	}

	void InnerProduct::Create(Oracle& oracle, const Scalar::Native& dotAB, const Scalar::Native* pA, const Scalar::Native* pB, const Modifier& mod)
	{
		Create(oracle, NULL, dotAB, pA, pB, mod);
	}

	void InnerProduct::Create(Oracle& oracle, Point::Native* pAB, const Scalar::Native& dotAB, const Scalar::Native* pA, const Scalar::Native* pB, const Modifier& mod)
	{
		Mode::Scope scope(Mode::Fast);

		assert(!mod.m_pAmbient); // supported only in verification mode

		Calculator c(mod);
		c.m_GenOrder = nCycles;
		c.m_ppSrc[0] = pA;
		c.m_ppSrc[1] = pB;

		if (pAB)
		{
			for (int j = 0; j < 2; j++)
				for (uint32_t i = 0; i < nDim; i++, c.m_Mm.m_Prepared++)
				{
					c.m_Mm.m_ppPrepared[c.m_Mm.m_Prepared] = &Context::get().m_Ipp.m_pGen_[j][i];
					c.m_Mod.Set(c.m_Mm.m_pKPrep[c.m_Mm.m_Prepared], c.m_ppSrc[j][i], i, j);
				}

			c.m_Mm.Calculate(*pAB);

			oracle << *pAB;
		}

		oracle << dotAB >> c.m_Cs.m_DotMultiplier;

		Point::Native comm;

		for (c.m_iCycle = 0; c.m_iCycle < nCycles; c.m_iCycle++)
		{
			c.CycleStart(oracle);

			for (int j = 0; j < 2; j++)
			{
				c.ExtractLR(j);

				c.m_Mm.Calculate(comm);
				m_pLR[c.m_iCycle][j] = comm;
			}

			c.CycleExpose(oracle, *this);

			c.Condense();

			c.CycleEnd();
		}

		for (int i = 0; i < 2; i++)
			m_pCondensed[i] = c.m_pVal[i][0];
	}

	bool InnerProduct::IsValid(const Point::Native& commAB, const Scalar::Native& dotAB, const Modifier& mod) const
	{
		if (BatchContext::s_pInstance)
			return IsValid(*BatchContext::s_pInstance, commAB, dotAB, mod);

		BatchContextEx<1> bc;
		return
			IsValid(bc, commAB, dotAB, mod) &&
			bc.Flush();
	}

	struct InnerProduct::Challenges
	{
		Scalar::Native m_DotMultiplier;
		Calculator::XSet m_X;

		Scalar::Native m_Mul1, m_Mul2;

		void Init(Oracle& oracle, const Scalar::Native& dotAB, const InnerProduct& v)
		{
			oracle << dotAB >> m_DotMultiplier;
			m_Mul1 = 1U;

			for (uint32_t iCycle = 0; iCycle < nCycles; iCycle++)
			{
				oracle >> m_X.m_Val[iCycle];

				m_Mul1 *= m_X.m_Val[iCycle];
				m_X.m_Val[iCycle] *= m_X.m_Val[iCycle];

				for (int j = 0; j < 2; j++)
					oracle << v.m_pLR[iCycle][j];
			}

			m_Mul2 = m_Mul1;
			m_Mul2 *= m_Mul1;
		}
	};

	bool InnerProduct::IsValid(BatchContext& bc, const Point::Native& commAB, const Scalar::Native& dotAB, const Modifier& mod) const
	{
		Mode::Scope scope(Mode::Fast);

		Oracle oracle;
		oracle << commAB;

		Challenges cs_;
		cs_.Init(oracle, dotAB, *this);

		bc.EquationBegin();
		bc.AddCasual(commAB, cs_.m_Mul2);

		return IsValid(bc, cs_, dotAB, mod);
	}

	bool InnerProduct::IsValid(BatchContext& bc, Challenges& cs_, const Scalar::Native& dotAB, const Modifier& mod) const
	{
		Mode::Scope scope(Mode::Fast);

		// Calculate the aggregated sum, consisting of sum of multiplications at once.
		// The expression we're calculating is:
		//
		// sum( LR[iCycle][0] * k[iCycle]^2 + LR[iCycle][0] * k[iCycle]^-2 )

		Scalar::Native k, mul0;
		if (mod.m_pAmbient)
		{
			mul0 = bc.m_Multiplier;
			bc.m_Multiplier *= *mod.m_pAmbient;
		}

		// calculate pairs of cs_.m_X.m_Val
		static_assert(!(nCycles & 1), "");
		const uint32_t nPairs = nCycles >> 1;
		Scalar::Native pPair[nPairs];

		for (uint32_t iPair = 0; iPair < nPairs; iPair++)
		{
			pPair[iPair] = cs_.m_X.m_Val[iPair << 1];
			pPair[iPair] *= cs_.m_X.m_Val[(iPair << 1) + 1];
		}

		for (uint32_t iCycle = 0; iCycle < nCycles; iCycle++)
		{
			const Point* pLR = m_pLR[iCycle];
			for (int j = 0; j < 2; j++)
			{
				if (j)
				{
					// all except this one.
					k = cs_.m_X.m_Val[iCycle ^ 1];

					for (uint32_t iPair = 0; iPair < nPairs; iPair++)
						if (iPair != (iCycle >> 1))
							k *= pPair[iPair];
				}
				else
				{
					k = cs_.m_Mul2;
					k *= cs_.m_X.m_Val[iCycle];
				}

				if (!bc.AddCasual(pLR[j], k))
					return false;
			}
		}

		// The expression we're calculating is: the transformed generator
		//
		// -sum( G_Condensed[j] * pCondensed[j] )
		// whereas G_Condensed[j] = Gen[j] * sum (k[iCycle]^(+/-)1 ), i.e. transformed (condensed) generators

		for (int j = 0; j < 2; j++)
		{
			MultiMac mmDummy;
			Calculator::Aggregator aggr(mmDummy, &cs_.m_X, NULL, mod, j, 0);
			aggr.m_pBatchCtx = &bc;

			k = m_pCondensed[j];
			k = -k;

			k *= (mod.m_pAmbient && j) ? mul0 : bc.m_Multiplier;

			k *= cs_.m_Mul1;

			aggr.Proceed(0, nCycles, k);
		}

		// subtract the new (mutated) dot product, add the original (claimed)
		k = m_pCondensed[0];
		k *= m_pCondensed[1];
		k = -k;
		k += dotAB;
		k *= cs_.m_DotMultiplier;
		k *= cs_.m_Mul2;

		bc.AddPrepared(BatchContext::s_Idx_GenDot, k);

		if (mod.m_pAmbient)
			bc.m_Multiplier = mul0; // restore it

		return true;
	}

	struct NonceGeneratorBp
		:public NonceGenerator
	{
		NonceGeneratorBp(const uintBig& seed)
			:NonceGenerator("bulletproof")
		{
			*this << seed;
		}
	};

	/////////////////////
	// Bulletproof
	void RangeProof::Confidential::Create(const Scalar::Native& sk, const Params::Create& cp, Oracle& oracle, const Point::Native* pHGen /* = nullptr */)
	{
		NoLeak<uintBig> seedSk;
        GenerateSeed(seedSk.V, sk, cp.m_Value, oracle);

		Nonces nonces(seedSk.V);
        BEAM_VERIFY(CoSign(nonces, sk, cp, oracle, Phase::SinglePass, pHGen));
	}

    void RangeProof::Confidential::GenerateSeed(uintBig& seedSk, const Scalar::Native& sk, Amount amount, Oracle& oracle)
    {
        // single-pass - use both deterministic and random seed for key blinding.
        // For more safety - use the current oracle state

        Oracle o(oracle); // copy
        GenRandom(seedSk);

        o
            << sk
            << seedSk
            << amount
            >> seedSk;
    }

	struct RangeProof::Confidential::ChallengeSet
	{
		Scalar::Native x, y, z, zz;
		void Init1(const Part1&, Oracle&);
		void Init2(const Part2&, Oracle&);
		void SetZZ();
	};

	struct RangeProof::Confidential::Vectors
	{
		Scalar::Native m_pS[2][InnerProduct::nDim];

		const Scalar::Native m_One = 1U;
		const Scalar::Native m_Two = 2U;

		Scalar::Native m_pZ[2];

		void Set(NonceGeneratorBp& ng)
		{
			for (int j = 0; j < 2; j++)
				for (uint32_t i = 0; i < InnerProduct::nDim; i++)
				{
					ng >> m_pS[j][i];
				}
		}

		void Set(const ChallengeSet& cs)
		{
			m_pZ[0] = cs.z;
			m_pZ[1] = cs.z - m_One;
		}

		void ToLR(const ChallengeSet& cs, Amount val)
		{
			// construct vectors l,r, use buffers pS
			// P - m_Mu*G
			Scalar::Native yPwr = m_One;
			Scalar::Native zz_twoPwr = cs.zz;
			Scalar::Native x;

			for (uint32_t i = 0; i < InnerProduct::nDim; i++)
			{
				uint32_t bit = 1 & (val >> i);

				m_pS[0][i] *= cs.x;
				m_pS[0][i] -= m_pZ[bit];

				m_pS[1][i] *= cs.x;
				m_pS[1][i] *= yPwr;

				x = m_pZ[!bit];
				x *= yPwr;
				x += zz_twoPwr;

				m_pS[1][i] += x;

				zz_twoPwr *= m_Two;
				yPwr *= cs.y;
			}
		}
	};

	void RangeProof::Params::Base::BlobSave(uint8_t* p, size_t n) const
	{
		assert(m_Blob.n <= n);
		size_t nPad = n - m_Blob.n;

		memset0(p, nPad);
		memcpy(p + nPad, m_Blob.p, m_Blob.n);
	}

	bool RangeProof::Params::Base::BlobRecover(const uint8_t* p, size_t n)
	{
		assert(m_Blob.n <= n);
		size_t nPad = n - m_Blob.n;

		if (!memis0(p, nPad))
			return false;

		memcpy(Cast::NotConst(m_Blob.p), p + nPad, m_Blob.n);
		return true;
	}

#pragma pack (push, 1)
	struct RangeProof::Params::Base::Packed
	{
		uint8_t m_pUser[sizeof(Scalar) - sizeof(Amount)];
		beam::uintBigFor<Amount>::Type m_Value; // for historical reasons: value should be here!
	};
#pragma pack (pop)

	bool RangeProof::Confidential::CoSign(const Nonces& nonces, const Scalar::Native& sk, const Params::Create& cp, Oracle& oracle, Phase::Enum ePhase, const Point::Native* pHGen /* = nullptr */)
	{
		NonceGeneratorBp nonceGen(cp.m_Seed.V);

		// A = G*alpha + vec(aL)*vec(G) + vec(aR)*vec(H)
		Scalar::Native alpha, ro;
		nonceGen >> alpha;

		// embed extra params into alpha
		NoLeak<Params::Base::Packed> cpp;
		cpp.V.m_Value = cp.m_Value;

		cp.BlobSave(cpp.V.m_pUser, sizeof(cpp.V.m_pUser));

		static_assert(sizeof(cpp) == sizeof(Scalar));
        BEAM_VERIFY(!ro.Import((Scalar&) cpp.V)); // if overflow - the params won't be recovered properly, there may be ambiguity

		alpha += ro;

		CalcA(m_Part1.m_A, alpha, cp.m_Value);

		// S = G*ro + vec(sL)*vec(G) + vec(sR)*vec(H)
		nonceGen >> ro;

		MultiMac_WithBufs<1, InnerProduct::nDim * 2 + 1> mm;
		mm.m_pKPrep[mm.m_Prepared] = ro;
		mm.m_ppPrepared[mm.m_Prepared++] = &Context::get().m_Ipp.G_;

		Vectors vecs;
		vecs.Set(nonceGen);

		for (int j = 0; j < 2; j++)
		{
			if (cp.m_pExtra)
				vecs.m_pS[j][0] += cp.m_pExtra[j];

			for (uint32_t i = 0; i < InnerProduct::nDim; i++)
			{
				mm.m_pKPrep[mm.m_Prepared] = vecs.m_pS[j][i];
				mm.m_ppPrepared[mm.m_Prepared++] = &Context::get().m_Ipp.m_pGen_[j][i];
			}
		}

		Point::Native comm;
		mm.Calculate(comm);

		m_Part1.m_S = comm;

		//if (Phase::Step1 == ePhase)
		//	return; // stop after A,S calculated

		// get challenges
		ChallengeSet cs;
		cs.Init1(m_Part1, oracle);
		cs.SetZZ();
		vecs.Set(cs);

		// calculate t1, t2 - parts of vec(L)*vec(R) which depend on (future) x and x^2.
		Scalar::Native t0(Zero), t1(Zero), t2(Zero);

		Scalar::Native l0, r0, rx, yPwr, zz_twoPwr;

		yPwr = vecs.m_One;
		zz_twoPwr = cs.zz;

		for (uint32_t i = 0; i < InnerProduct::nDim; i++)
		{
			uint32_t bit = 1 & (cp.m_Value >> i);


			const Scalar::Native& lx = vecs.m_pS[0][i];

			r0 = vecs.m_pZ[!bit];
			r0 *= yPwr;
			r0 += zz_twoPwr;

			rx = yPwr;
			rx *= vecs.m_pS[1][i];

			zz_twoPwr *= vecs.m_Two;
			yPwr *= cs.y;

			l0 = -vecs.m_pZ[bit];
			t0 += l0 * r0;
			t1 += l0 * rx;
			t1 += lx * r0;
			t2 += lx * rx;
		}

		if (Phase::Finalize != ePhase) // otherwise m_Part2 already contains the whole aggregate
		{
			Point::Native comm2;
			nonces.AddInfo1(comm, comm2);

			if (Tag::IsCustom(pHGen))
			{
				// since we need 2 multiplications - prepare it explicitly.
				MultiMac::Casual mc;
				mc.Init(*pHGen);

				MultiMac mm2;
				mm2.m_pCasual = &mc;
				mm2.m_Casual = 1;
				Point::Native comm3;

				mm2.m_pKCasual = &t1;
				mm2.Calculate(comm3);
				comm += comm3;

				mm2.m_pKCasual = &t2;
				mm2.Calculate(comm3);
				comm2 += comm3;
			}
			else
			{
				comm += Context::get().H_Big * t1;
				comm2 += Context::get().H_Big * t2;
			}

			if (Phase::SinglePass != ePhase)
			{
				Point::Native p;
				if (!p.Import(m_Part2.m_T1))
					return false;
				comm += p;

				if (!p.Import(m_Part2.m_T2))
					return false;
				comm2 += p;
			}

			m_Part2.m_T1 = comm;
			m_Part2.m_T2 = comm2;
		}

		if (Phase::Step2 == ePhase)
			return true; // stop after T1,T2 calculated

		cs.Init2(m_Part2, oracle); // get challenge 

		// m_TauX = tau2*x^2 + tau1*x + sk*z^2
		nonces.AddInfo2(l0, sk, cs);

		if (Phase::SinglePass != ePhase)
			l0 += m_Part3.m_TauX;

		m_Part3.m_TauX = l0;

		// m_Mu = alpha + ro*x
		l0 = ro;
		l0 *= cs.x;
		l0 += alpha;
		m_Mu = l0;

		// m_tDot
		l0 = t0;

		r0 = t1;
		r0 *= cs.x;
		l0 += r0;

		r0 = t2;
		r0 *= cs.x;
		r0 *= cs.x;
		l0 += r0;

		m_tDot = l0;

		// construct vectors l,r, use buffers pS
		// P - m_Mu*G
		vecs.ToLR(cs, cp.m_Value);

		Scalar::Native& yInv = alpha; // alias
		yInv.SetInv(cs.y);

		InnerProduct::Modifier::Channel ch1;
		ch1.SetPwr(yInv);
		InnerProduct::Modifier mod;
		mod.m_ppC[1] = &ch1;

		m_P_Tag.Create(oracle, l0, vecs.m_pS[0], vecs.m_pS[1], mod);

		return true;
	}

	void RangeProof::Confidential::CalcA(Point& res, const Scalar::Native& alpha, Amount v)
	{
		Point::Native comm = Context::get().G * alpha;

		{
			NoLeak<Point::Compact> ge_s;

			for (uint32_t i = 0; i < InnerProduct::nDim; i++)
			{
				uint32_t iBit = 1 & (v >> i);

				// protection against side-channel attacks
				object_cmov(ge_s.V, Context::get().m_Ipp.m_pGet1_Minus[i], 0 == iBit);
				object_cmov(ge_s.V, Context::get().m_Ipp.m_pGen_[0][i].m_Fast.m_pPt[0], 1 == iBit);

				comm += ge_s.V;
			}
		}

		res = comm;
	}

	bool RangeProof::Confidential::Recover(Oracle& oracle, Params::Recover& cp) const
	{
		NonceGeneratorBp nonceGen(cp.m_Seed.V);

		Scalar::Native alpha_minus_params, ro;
		nonceGen >> alpha_minus_params;
		nonceGen >> ro;

		// get challenges
		ChallengeSet cs;
		cs.Init1(m_Part1, oracle);
		cs.Init2(m_Part2, oracle);

		// m_Mu = alpha + ro*x
		// alpha = m_Mu - ro*x = alpha_minus_params + params
		// params = m_Mu - ro*x - alpha_minus_params

		ro *= cs.x;
		Scalar::Native params = alpha_minus_params;
		params += ro;
		params = -params;
		params += m_Mu;

		Params::Base::Packed cpp;
		static_assert(sizeof(cpp) == sizeof(Scalar));
		((Scalar&) cpp) = params;

		if (!cp.BlobRecover(cpp.m_pUser, sizeof(cpp.m_pUser)))
			return false;

		cpp.m_Value.Export(cp.m_Value);

		// by now the probability of false positive if 2^-64 (padding is 8 bytes typically)
		// Calculate m_Part1.m_A, which depends on alpha and the value.

		alpha_minus_params += params; // just alpha
		Point ptA;
		CalcA(ptA, alpha_minus_params, cp.m_Value);

		if (ptA != m_Part1.m_A)
			return false; // the probability of false positive should be negligible

		bool bRecoverSk = (cp.m_pSeedSk && cp.m_pSk);
		if (bRecoverSk || cp.m_pExtra)
			cs.SetZZ();

		if (bRecoverSk)
		{

			// recover the blinding factor
			Scalar::Native& sk = *cp.m_pSk; // alias
			sk = Zero;

			Nonces nonces(*cp.m_pSeedSk);

			Scalar::Native k;
			nonces.AddInfo2(k, cs);

			sk = m_Part3.m_TauX;
			k = -k;
			sk += k;

			k.SetInv(cs.zz);
			sk *= k;
		}

		if (cp.m_pExtra)
		{
			// recover 2 more scalars
			Vectors vecs;
			vecs.Set(nonceGen);
			vecs.Set(cs);

			vecs.ToLR(cs, cp.m_Value);

			InnerProduct::CalculatorBase c;
			c.m_ppSrc[0] = vecs.m_pS[0];
			c.m_ppSrc[1] = vecs.m_pS[1];

			oracle << m_tDot >> c.m_Cs.m_DotMultiplier;

			for (c.m_iCycle = 0; c.m_iCycle < InnerProduct::nCycles; c.m_iCycle++)
			{
				c.CycleStart(oracle);
				c.CycleExpose(oracle, m_P_Tag);
				c.CondenseBase();
				c.CycleEnd();
			}

			for (int j = 0; j < 2; j++)
			{
				Scalar::Native kDiff = m_P_Tag.m_pCondensed[j];
				kDiff -= c.m_pVal[j][0]; // actual differnce

				// now let's estimate the difference that would be if extra == 1.
				Scalar::Native kDiff1 = cs.x; // After ToLR

				for (c.m_iCycle = 0; c.m_iCycle < InnerProduct::nCycles; c.m_iCycle++)
					kDiff1 *= c.m_Cs.m_pX[j].m_Val[c.m_iCycle];

				Scalar::Native& x = cp.m_pExtra[j]; // alias
				x.SetInv(kDiff1);
				x *= kDiff;
			}
		}

		return true;
	}

	void RangeProof::Confidential::Nonces::Init(const uintBig& seedSk)
	{
		NonceGenerator("bp-key")
			<< seedSk
			>> m_tau1
			>> m_tau2;
	}

	void RangeProof::Confidential::Nonces::AddInfo1(Point::Native& ptT1, Point::Native& ptT2) const
	{
		ptT1 = Context::get().G * m_tau1;
		ptT2 = Context::get().G * m_tau2;
	}

	void RangeProof::Confidential::Nonces::AddInfo2(Scalar::Native& taux, const ChallengeSet& cs) const
	{
		// m_TauX = tau2*x^2 + tau1*x + sk*z^2
		taux = m_tau2;
		taux *= cs.x;
		taux *= cs.x;

		taux += m_tau1 * cs.x;
	}

	void RangeProof::Confidential::Nonces::AddInfo2(Scalar::Native& taux, const Scalar::Native& sk, const ChallengeSet& cs) const
	{
		AddInfo2(taux, cs);
		taux += sk * cs.zz; // UTXO blinding factor (or part of it in case of multi-sig)
	}

	bool RangeProof::Confidential::MultiSig::CoSignPart(const Nonces& nonces, Part2& p2)
	{
		Point::Native ptT1, ptT2;
		nonces.AddInfo1(ptT1, ptT2);

		Point::Native pt;
		if (!pt.Import(p2.m_T1))
			return false;
		pt += ptT1;
		p2.m_T1 = pt;

		if (!pt.Import(p2.m_T2))
			return false;
		pt += ptT2;
		p2.m_T2 = pt;

		return true;
	}

	void RangeProof::Confidential::MultiSig::CoSignPart(const Nonces& nonces, const Scalar::Native& sk, Oracle& oracle, Part3& p3) const
	{
		ChallengeSet cs;
		cs.Init1(m_Part1, oracle);
		cs.SetZZ();
		cs.Init2(m_Part2, oracle);

		Scalar::Native taux;
		nonces.AddInfo2(taux, sk, cs);

		taux += Scalar::Native(p3.m_TauX);
		p3.m_TauX = taux;
	}

	void RangeProof::Confidential::ChallengeSet::Init1(const Part1& p1, Oracle& oracle)
	{
		oracle << p1.m_A << p1.m_S;
		oracle >> y;
		oracle >> z;
	}

	void RangeProof::Confidential::ChallengeSet::SetZZ()
	{
		zz = z;
		zz *= z;
	}

	void RangeProof::Confidential::ChallengeSet::Init2(const Part2& p2, Oracle& oracle)
	{
		oracle << p2.m_T1 << p2.m_T2;
		oracle >> x;
	}

	bool RangeProof::Confidential::IsValid(const Point::Native& commitment, Oracle& oracle, const Point::Native* pHGen /* = nullptr */) const
	{
		if (InnerProduct::BatchContext::s_pInstance)
			return IsValid(commitment, oracle, *InnerProduct::BatchContext::s_pInstance, pHGen);

		InnerProduct::BatchContextEx<1> bc;
		return
			IsValid(commitment, oracle, bc, pHGen) &&
			bc.Flush();
	}

	bool RangeProof::Confidential::IsValid(const Point::Native& commitment, Oracle& oracle, InnerProduct::BatchContext& bc, const Point::Native* pHGen /* = nullptr */) const
	{
		bool bCustom = Tag::IsCustom(pHGen);

		Mode::Scope scope(Mode::Fast);

		ChallengeSet cs;
		cs.Init1(m_Part1, oracle);
		cs.SetZZ();
		cs.Init2(m_Part2, oracle);

		InnerProduct::Modifier::Channel ch1;

		// powers of cs.y in reverse order
		ch1.m_pV[InnerProduct::nDim - 1] = 1U;
		for (uint32_t i = InnerProduct::nDim - 1; i--; )
			ch1.m_pV[i] = ch1.m_pV[i + 1] * cs.y;

		Scalar::Native xx, zz, tDot;

		// calculate delta(y,z) = (z - z^2) * sumY - z^3 * sum2
		Scalar::Native delta, sum2, sumY(Zero);

		for (uint32_t i = 0; i < InnerProduct::nDim; i++)
			sumY += ch1.m_pV[i];

		const Scalar::Native& yPwrMax = ch1.m_pV[0];

		sum2 = Amount(-1);

		zz = cs.z * cs.z;

		delta = cs.z;
		delta += -zz;
		delta *= sumY;

		sum2 *= zz;
		sum2 *= cs.z;
		delta += -sum2;

		// H_Big * m_tDot + G * m_TauX =?= commitment * z^2 + H_Big * delta(y,z) + m_T1*x + m_T2*x^2
		// H_Big * (m_tDot - delta(y,z)) + G * m_TauX =?= commitment * z^2 + m_T1*x + m_T2*x^2


		xx = cs.x * cs.x;

		Point::Native p;

		bc.EquationBegin();
		bc.AddCasual(commitment, -zz);

		if (!bc.AddCasual(m_Part2.m_T1, -cs.x))
			return false;
		if (!bc.AddCasual(m_Part2.m_T2, -xx))
			return false;

		tDot = m_tDot;
		sumY = tDot;
		sumY += -delta;

		bc.AddPrepared(InnerProduct::BatchContext::s_Idx_G, m_Part3.m_TauX);

		if (bCustom)
			bc.AddCasual(*pHGen, sumY);
		else
			bc.AddPrepared(InnerProduct::BatchContext::s_Idx_H, sumY);

		// (P - m_Mu*G) + m_Mu*G =?= m_A + m_S*x - vec(G)*vec(z) + vec(H)*( vec(z) + vec(z^2*2^n*y^-n) )

		bc.EquationBegin();

		delta = bc.m_Multiplier;
		bc.m_Multiplier *= yPwrMax;

		InnerProduct::Challenges cs_;
		cs_.Init(oracle, tDot, m_P_Tag);

		cs.z *= cs_.m_Mul2;

		bc.AddPrepared(InnerProduct::BatchContext::s_Idx_Aux2, cs.z);

		sumY = m_Mu;
		sumY *= cs_.m_Mul2;

		bc.AddPrepared(InnerProduct::BatchContext::s_Idx_G, -sumY);

		if (!bc.AddCasual(m_Part1.m_S, cs.x * cs_.m_Mul2))
			return false;

		if (!bc.AddCasual(m_Part1.m_A, cs_.m_Mul2))
			return false;

		Scalar::Native& zMul = sumY; // alias
		zMul = cs.z * bc.m_Multiplier;

		bc.m_Multiplier = delta; // restore multiplier before it was multiplied by yPwrMax

		Scalar::Native& pwr = delta; // alias
		Scalar::Native mul;

		mul = Context::get().m_Ipp.m_2Inv;
		mul *= cs.y; // y/2
		pwr = zz;
		pwr *= (Amount(1) << (InnerProduct::nDim - 1)); // 2 ^ (nDim-1)
		pwr *= cs_.m_Mul2;
		pwr *= bc.m_Multiplier;

		for (uint32_t i = InnerProduct::nDim - 1; ; )
		{
			sum2 = pwr;
			sum2 += zMul;

			bc.AddPreparedM(InnerProduct::nDim + i, sum2);

			if (!i--)
				break;

			pwr *= mul;
		}

		// finally check the inner product
		InnerProduct::Modifier mod;
		mod.m_ppC[1] = &ch1;
		mod.m_pAmbient = &yPwrMax;

		return m_P_Tag.IsValid(bc, cs_, tDot, mod);
	}

	int RangeProof::Confidential::cmp(const Confidential& x) const
	{
		// don't care
		return memcmp(this, &x, sizeof(*this));
	}

} // namespace ECC
