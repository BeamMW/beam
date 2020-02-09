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

#define USE_BASIC_CONFIG

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wunused-function"
#else
#	pragma warning (push, 0) // suppress warnings from secp256k1
#	pragma warning (disable: 4706 4701) // assignment within conditional expression
#endif

#include "ecc_min.h"
#include <assert.h>

#include "secp256k1-zkp/src/group_impl.h"
#include "secp256k1-zkp/src/scalar_impl.h"
#include "secp256k1-zkp/src/field_impl.h"
#include "secp256k1-zkp/src/hash_impl.h"

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic pop
#else
#	pragma warning (default: 4706 4701)
#	pragma warning (pop)
#endif

namespace ECC_Min
{

#ifdef USE_SCALAR_4X64
	typedef uint64_t secp256k1_scalar_uint;
#else // USE_SCALAR_4X64
	typedef uint32_t secp256k1_scalar_uint;
#endif // USE_SCALAR_4X64

	/////////////////////
	// BatchNormalizer
	struct BatchNormalizer
	{
		struct Element
		{
			secp256k1_gej* m_pPoint;
			secp256k1_fe* m_pFe; // temp
		};

		virtual void Reset() = 0;
		virtual bool MoveNext(Element&) = 0;
		virtual bool MovePrev(Element&) = 0;

		void Normalize();
		void ToCommonDenominator(secp256k1_fe& zDenom);

		static void get_As(secp256k1_ge&, const secp256k1_gej& ptNormalized);
		static void get_As(secp256k1_ge_storage&, const secp256k1_gej& ptNormalized);

	private:
		void NormalizeInternal(secp256k1_fe&, bool bNormalize);
	};

	void BatchNormalizer::Normalize()
	{
		secp256k1_fe zDenom;
		NormalizeInternal(zDenom, true);
	}

	void BatchNormalizer::ToCommonDenominator(secp256k1_fe& zDenom)
	{
		secp256k1_fe_set_int(&zDenom, 1);
		NormalizeInternal(zDenom, false);
	}

	void secp256k1_gej_rescale_XY(secp256k1_gej& gej, const secp256k1_fe& z)
	{
		// equivalent of secp256k1_gej_rescale, but doesn't change z coordinate
		// A bit more effective when the value of z is know in advance (such as when normalizing)
		secp256k1_fe zz;
		secp256k1_fe_sqr(&zz, &z);

		secp256k1_fe_mul(&gej.x, &gej.x, &zz);
		secp256k1_fe_mul(&gej.y, &gej.y, &zz);
		secp256k1_fe_mul(&gej.y, &gej.y, &z);
	}

	void BatchNormalizer::NormalizeInternal(secp256k1_fe& zDenom, bool bNormalize)
	{
		bool bEmpty = true;
		Element elPrev = { 0 }; // init not necessary, just suppress the warning
		Element el = { 0 };

		for (Reset(); ; )
		{
			if (!MoveNext(el))
				break;

			if (bEmpty)
			{
				bEmpty = false;
				*el.m_pFe = el.m_pPoint->z;
			}
			else
				secp256k1_fe_mul(el.m_pFe, elPrev.m_pFe, &el.m_pPoint->z);

			elPrev = el;
		}

		if (bEmpty)
			return;

		if (bNormalize)
			secp256k1_fe_inv(&zDenom, elPrev.m_pFe); // the only expensive call

		while (true)
		{
			bool bFetched = MovePrev(el);
			if (bFetched) 
				secp256k1_fe_mul(elPrev.m_pFe, el.m_pFe, &zDenom);
			else
				*elPrev.m_pFe = zDenom;

			secp256k1_fe_mul(&zDenom, &zDenom, &elPrev.m_pPoint->z);

			secp256k1_gej_rescale_XY(*elPrev.m_pPoint, *elPrev.m_pFe);
			elPrev.m_pPoint->z = zDenom;

			if (!bFetched)
				break;

			elPrev = el;
		}
	}

	void BatchNormalizer::get_As(secp256k1_ge& ge, const secp256k1_gej& ptNormalized)
	{
		ge.x = ptNormalized.x;
		ge.y = ptNormalized.y;
		ge.infinity = ptNormalized.infinity;
	}

	void BatchNormalizer::get_As(secp256k1_ge_storage& ge_s, const secp256k1_gej& ptNormalized)
	{
		secp256k1_ge ge;
		get_As(ge, ptNormalized);
		secp256k1_ge_to_storage(&ge_s, &ge);
	}


	/////////////////////
	// MultiMac
	void MultiMac::Reset()
	{
		m_Casual = 0;
		m_ws.Reset();
	}

	struct MultiMac::WnafBase::Context
	{
		const secp256k1_scalar_uint* m_p;
		unsigned int m_Flag;
		unsigned int m_iBit;
		unsigned int m_Carry;

		unsigned int NextOdd();

		void OnBit(unsigned int& res, unsigned int x)
		{
			if (x)
			{
				res |= m_Flag;
				m_Carry = 0;
			}
		}
	};

	unsigned int MultiMac::WnafBase::Context::NextOdd()
	{
		const unsigned int nWordBits = sizeof(*m_p) << 3;
		const unsigned int nMsk = nWordBits - 1;

		unsigned int res = 0;

		while (true)
		{
			if (m_iBit >= ECC_Min::nBits)
			{
				OnBit(res, m_Carry);

				if (!res)
					break;
			}
			else
			{
				unsigned int n = m_p[m_iBit / nWordBits] >> (m_iBit & nMsk);
				OnBit(res, (1 & n) != m_Carry);
			}

			m_iBit++;
			res >>= 1;

			if (1 & res)
				break;
		}

		return res;
	}

	void MultiMac::WnafBase::Shared::Reset()
	{
		memset(m_pTable, 0, sizeof(m_pTable));
	}

	unsigned int MultiMac::WnafBase::Shared::Add(Entry* pTrg, const secp256k1_scalar& k, unsigned int nWndBits, WnafBase& wnaf, Index iElement)
	{
		const unsigned int nWndConsume = nWndBits + 1;

		Context ctx;
		ctx.m_p = k.d;
		ctx.m_iBit = 0;
		ctx.m_Carry = 0;
		ctx.m_Flag = 1 << nWndConsume;

		uint8_t iEntry = 0;

		for ( ; ; iEntry++)
		{
			unsigned int nOdd = ctx.NextOdd();
			if (!nOdd)
				break;

			assert(!ctx.m_Carry);
			assert(1 & nOdd);
			assert(!(nOdd >> nWndConsume));

			assert(ctx.m_iBit >= nWndConsume);
			unsigned int iIdx = ctx.m_iBit - nWndConsume;
			assert(iIdx < _countof(m_pTable));

			Entry& x = pTrg[iEntry];
			x.m_iBit = static_cast<uint16_t>(iIdx);

			if (nOdd >> nWndBits)
			{
				// hi bit is ON
				nOdd ^= (1 << nWndConsume) - 2;

				assert(1 & nOdd);
				assert(!(nOdd >> nWndBits));

				x.m_Odd = static_cast<int16_t>(nOdd) | Entry::s_Negative;

				ctx.m_Carry = 1;
			}
			else
				x.m_Odd = static_cast<int16_t>(nOdd);
		}

		unsigned int ret = iEntry--;
		if (ret)
		{
			// add the highest bit
			const Entry& x = pTrg[iEntry];
			Link& lnk = m_pTable[x.m_iBit];
			
			wnaf.m_Next = lnk;
			lnk.m_iElement = iElement;
			lnk.m_iEntry = iEntry;
		}

		return ret;
	}

	unsigned int MultiMac::WnafBase::Shared::Fetch(unsigned int iBit, WnafBase& wnaf, const Entry* pE, bool& bNeg)
	{
		Link& lnkTop = m_pTable[iBit]; // alias
		Link lnkThis = lnkTop;

		const Entry& e = pE[lnkThis.m_iEntry];
		assert(e.m_iBit == iBit);

		lnkTop = wnaf.m_Next; // pop this entry

		if (lnkThis.m_iEntry)
		{
			// insert next entry
			lnkThis.m_iEntry--;

			const Entry& e2 = pE[lnkThis.m_iEntry];
			assert(e2.m_iBit < iBit);

			Link& lnkTop2 = m_pTable[e2.m_iBit];
			wnaf.m_Next = lnkTop2;
			lnkTop2 = lnkThis;
		}

		unsigned int nOdd = e.m_Odd;
		assert(1 & nOdd);

		bNeg = !!(e.m_Odd & e.s_Negative);

		if (bNeg)
			nOdd &= ~e.s_Negative;

		return nOdd;
	}

	struct MultiMac::Normalizer
		:public BatchNormalizer
	{
		Casual* m_pCasual;
		Index m_Casual;

		struct Cursor
		{
			Index m_iElement;
			uint32_t m_iEntry;
		};

		Cursor m_Cursor;

		void FromCursor(Element& el, const Cursor& cu) const;
		bool MoveBkwd(Cursor& cu) const;

		virtual void Reset() override;
		virtual bool MoveNext(Element& el) override;

		virtual bool MovePrev(Element& el) override;
	};

	void MultiMac::Normalizer::FromCursor(Element& el, const Cursor& cu) const
	{
		Casual& x = m_pCasual[cu.m_iElement];

		el.m_pPoint = x.m_pPt + cu.m_iEntry;
		el.m_pFe = x.m_pFe + cu.m_iEntry;
	}

	bool MultiMac::Normalizer::MoveBkwd(Cursor& cu) const
	{
		while (true)
		{
			if (cu.m_iElement < m_Casual)
			{
				Casual& x = m_pCasual[cu.m_iElement];

				assert(cu.m_iEntry <= x.m_nNeeded);
				x; // suppress warning in release build

				if (cu.m_iEntry)
				{
					cu.m_iEntry--;
					break;
				}
			}

			if (!cu.m_iElement)
				return false;

			cu.m_iElement--;

			Casual& x = m_pCasual[cu.m_iElement];
			cu.m_iEntry = x.m_nNeeded;
		}

		return true;
	}

	void MultiMac::Normalizer::Reset()
	{
		memset(&m_Cursor, 0, sizeof(m_Cursor));
	}

	bool MultiMac::Normalizer::MoveNext(Element& el)
	{
		while (true)
		{
			if (m_Cursor.m_iElement == m_Casual)
				return false;

			Casual& x = m_pCasual[m_Cursor.m_iElement];

			if (m_Cursor.m_iEntry < x.m_nNeeded)
				break;

			m_Cursor.m_iElement++;
			m_Cursor.m_iEntry = 0;

		}

		FromCursor(el, m_Cursor);
		m_Cursor.m_iEntry++;

		return true;
	}

	bool MultiMac::Normalizer::MovePrev(Element& el)
	{
		Cursor cu1 = m_Cursor;
		if (!MoveBkwd(cu1))
			return false;

		Cursor cu2 = cu1;
		if (!MoveBkwd(cu2))
			return false;

		m_Cursor = cu1;
		FromCursor(el, cu2);
		return true;
	}

	void MultiMac::Add(Casual& x, const secp256k1_gej& gej, const secp256k1_scalar& k)
	{
		Index iEntry = m_Casual++;

		if (secp256k1_gej_is_infinity(&gej))
		{
			x.m_nNeeded = 0;
			return;
		}

		x.m_pPt[0] = gej;

		unsigned int nEntries = x.m_Wnaf.Init(m_ws, k, iEntry + 1);
		assert(nEntries <= _countof(x.m_Wnaf.m_pVals));

		{
			// Find highest needed element, calculate all the needed ones
			x.m_nNeeded = 0;
			for (unsigned int i = 0; i < nEntries; i++)
			{
				const WnafBase::Entry& e = x.m_Wnaf.m_pVals[i];

				unsigned int nOdd = e.m_Odd & ~e.s_Negative;
				assert(nOdd & 1);

				unsigned int nElem = (nOdd >> 1);

				if (x.m_nNeeded < nElem + 1)
					x.m_nNeeded = nElem + 1;
			}
			assert(x.m_nNeeded <= Casual::nCount);

			if (x.m_nNeeded <= 1)
				return;
		}

		// Calculate all the needed elements
		secp256k1_gej ptX2;
		secp256k1_gej_double_var(&ptX2, &gej, nullptr);

		for (uint32_t nPrepared = 1; nPrepared < x.m_nNeeded; nPrepared++)
			secp256k1_gej_add_var(x.m_pPt + nPrepared, x.m_pPt + nPrepared - 1, &ptX2, nullptr);
	}

	void MultiMac::Calculate(secp256k1_gej& res, Casual* pCasual)
	{
		secp256k1_gej_set_infinity(&res);

		// Bring everything to the same denominator
		secp256k1_fe zDenom;
		Normalizer nrm;
		nrm.m_pCasual = pCasual;
		nrm.m_Casual = m_Casual;
		nrm.ToCommonDenominator(zDenom);

		
		for (unsigned int iBit = ECC_Min::nBits + 1; iBit--; ) // extra bit may be necessary because of interleaving
		{
			if (!secp256k1_gej_is_infinity(&res))
				secp256k1_gej_double_var(&res, &res, nullptr);

			WnafBase::Link& lnkC = m_ws.m_pTable[iBit]; // alias
			while (lnkC.m_iElement)
			{
				Casual& x = pCasual[lnkC.m_iElement - 1];
				Casual::Wnaf& wnaf = x.m_Wnaf;

				bool bNeg;
				unsigned int nOdd = wnaf.Fetch(m_ws, iBit, bNeg);

				unsigned int nElem = (nOdd >> 1);
				assert(nElem < x.m_nNeeded);

				secp256k1_ge ge;
				BatchNormalizer::get_As(ge, x.m_pPt[nElem]);

				if (bNeg)
					secp256k1_ge_neg(&ge, &ge);

				secp256k1_gej_add_ge_var(&res, &res, &ge, nullptr);
			}
		}

		// fix denominator
		secp256k1_fe_mul(&res.z, &res.z, &zDenom);
	}





} // namespace ECC_Min

