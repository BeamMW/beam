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

	struct MultiMac::BitWalker
	{
		int m_Word;
		secp256k1_scalar_uint m_Msk;

		static const unsigned int s_WordBits = sizeof(secp256k1_scalar_uint) * 8;

		void SetPos(uint8_t iBit)
		{

			m_Word = iBit / s_WordBits;
			m_Msk = secp256k1_scalar_uint(1) << (iBit & (s_WordBits - 1));
		}

		void MoveUp()
		{
			if (!(m_Msk <<= 1))
			{
				m_Msk = 1;
				m_Word++;
			}
		}

		void MoveDown()
		{
			if (!(m_Msk >>= 1))
			{
				const uint16_t nWordBits = sizeof(secp256k1_scalar_uint) * 8;
				m_Msk = secp256k1_scalar_uint(1) << (nWordBits - 1);

				m_Word--;
			}
		}

		secp256k1_scalar_uint get(const secp256k1_scalar& k) const
		{
			return k.d[m_Word] & m_Msk;
		}

		void xor(secp256k1_scalar& k) const
		{
			k.d[m_Word] ^= m_Msk;
		}
	};


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
	void MultiMac::WNaf::Cursor::MoveNext(const secp256k1_scalar& k)
	{
		BitWalker bw;
		bw.SetPos(--m_iBit);

		// find next nnz bit
		for (; ; m_iBit--, bw.MoveDown())
		{
			if (bw.get(k))
				break;

			if (!m_iBit)
			{
				// end
				m_iBit = 1;
				m_iElement = s_HiBit;
				return;
			}
		}

		uint8_t nOdd = 1;

		uint8_t nWndBits = Prepared::nBits - 1;
		if (nWndBits > m_iBit)
			nWndBits = m_iBit;

		for (uint8_t i = 0; i < nWndBits; i++, m_iBit--)
		{
			bw.MoveDown();
			nOdd = (nOdd << 1) | (bw.get(k) != 0);
		}

		for (; !(1 & nOdd); m_iBit++)
			nOdd >>= 1;

		m_iElement = nOdd >> 1;
	}

	bool MultiMac::Scalar::SplitPosNeg()
	{
#ifdef ECC_Min_MultiMac_Interleaved
		static_assert(2 == s_Directions);

		memset(m_pK[1].d, 0, sizeof(m_pK[1].d));

		uint8_t iBit = 0;
		BitWalker bw;
		bw.m_Word = 0;
		bw.m_Msk = 1;

		while (true)
		{
			// find nnz bit
			while (true)
			{
				if (iBit >= ECC_Min::nBits - Prepared::nBits)
					return false;

				if (bw.get(m_pK[0]))
					break;

				iBit++;
				bw.MoveUp();
			}

			BitWalker bw0 = bw;

			iBit += Prepared::nBits;
			for (uint32_t i = 0; i < Prepared::nBits; i++)
				bw.MoveUp(); // akward

			if (!bw.get(m_pK[0]))
				continue; // interleaving is not needed

			// set negative bits
			bw0.xor(m_pK[0]);
			bw0.xor(m_pK[1]);

			for (uint8_t i = 1; i < Prepared::nBits; i++)
			{
				bw0.MoveUp();

				secp256k1_scalar_uint val = bw0.get(m_pK[0]);
				bw0.xor(m_pK[!val]);
			}

			// propagate carry
			while (true)
			{
				bw.xor(m_pK[0]);
				if (bw.get(m_pK[0]))
					break;

				if (! ++iBit)
					return true; // carry goes outside

				bw.MoveUp();
			}
		}
#endif // ECC_Min_MultiMac_Interleaved

		return false;
	}

	void MultiMac::Context::Calculate() const
	{
		secp256k1_gej_set_infinity(m_pRes);

		for (unsigned int i = 0; i < m_Count; i++)
		{
			WNaf& wnaf = m_pWnaf[i];
			Scalar& s = m_pS[i];

			wnaf.m_pC[0].m_iBit = 0;


			bool bCarry = s.SplitPosNeg();
			if (bCarry)
				wnaf.m_pC[0].m_iElement = WNaf::Cursor::s_HiBit;
			else
				wnaf.m_pC[0].MoveNext(s.m_pK[0]);

#ifdef ECC_Min_MultiMac_Interleaved
			wnaf.m_pC[1].m_iBit = 0;
			wnaf.m_pC[1].MoveNext(s.m_pK[1]);
#endif // ECC_Min_MultiMac_Interleaved
		}
		
		for (uint16_t iBit = ECC_Min::nBits + 1; iBit--; ) // extra bit may be necessary because of interleaving
		{
			if (!secp256k1_gej_is_infinity(m_pRes))
				secp256k1_gej_double_var(m_pRes, m_pRes, nullptr);

			for (unsigned int i = 0; i < m_Count; i++)
			{
				WNaf& wnaf = m_pWnaf[i];

				for (unsigned int j = 0; j < s_Directions; j++)
				{
					WNaf::Cursor& wc = wnaf.m_pC[j];

					if (static_cast<uint8_t>(iBit) != wc.m_iBit)
						continue;

					// special case: resolve 256-0 ambiguity
					if ((wc.m_iElement ^ static_cast<uint8_t>(iBit >> 1))& WNaf::Cursor::s_HiBit)
						continue;

					secp256k1_ge ge;
					secp256k1_ge_from_storage(&ge, m_pPrep[i].m_pPt + (wc.m_iElement & ~WNaf::Cursor::s_HiBit));

					if (j)
						secp256k1_ge_neg(&ge, &ge);

					secp256k1_gej_add_ge_var(m_pRes, m_pRes, &ge, nullptr);

					if (iBit)
						wc.MoveNext(m_pS[i].m_pK[j]);
				}
			}
		}
	}


} // namespace ECC_Min

