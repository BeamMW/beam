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
		m_Prepared = 0;
	}

	void MultiMac::WNafCursor::Reset()
	{
		m_iBit = 0;
		m_iElement = 0;
	}

	bool MultiMac::WNafCursor::FindCarry(const secp256k1_scalar& k)
	{
		// find next nnz bit
		for (; ; m_iBit--)
		{
			if (get_Bit(k, m_iBit))
				break;

			if (!m_iBit)
				return false;
		}

		return true;
	}

	void MultiMac::WNafCursor::MoveAfterCarry(const secp256k1_scalar& k)
	{
		uint8_t nOdd = 1;

		uint8_t nWndBits = Prepared::nBits - 1;
		if (nWndBits > m_iBit)
			nWndBits = m_iBit;

		for (uint8_t i = 0; i < nWndBits; i++)
			nOdd = (nOdd << 1) | get_Bit(k, --m_iBit);

		for (; !(1 & nOdd); m_iBit++)
			nOdd >>= 1;

		m_iElement = nOdd >> 1;
	}

	void MultiMac::WNafCursor::MoveNext(const secp256k1_scalar& k)
	{
		m_iBit--;

		if (FindCarry(k))
			MoveAfterCarry(k);
		else
		{
			m_iBit = 1;
			m_iElement = s_HiBit;
		}
	}

	uint8_t MultiMac::WNafCursor::get_Bit(const secp256k1_scalar& k, uint8_t iBit)
	{
		const uint16_t nWordBits = sizeof(secp256k1_scalar_uint) * 8;
		return 1 & (k.d[iBit / nWordBits] >> (iBit & (nWordBits - 1)));
	}

	void MultiMac::WNafCursor::xor_Bit(secp256k1_scalar& k, uint8_t iBit)
	{
		const uint16_t nWordBits = sizeof(secp256k1_scalar_uint) * 8;

		secp256k1_scalar_uint& x = k.d[iBit / nWordBits];
		secp256k1_scalar_uint msk = uint8_t(1) << (iBit & (nWordBits - 1));

		x ^= msk;
	}

	bool MultiMac::WNafCursor::SplitPosNeg(secp256k1_scalar& k0, secp256k1_scalar& k1)
	{
		memset(k1.d, 0, sizeof(k1.d));

		uint8_t iBit = 0;

		while (true)
		{
			// find nnz bit
			for (; ; iBit++)
			{
				if (iBit >= ECC_Min::nBits - Prepared::nBits)
					return false;

				if (get_Bit(k0, iBit))
					break;
			}

			iBit += Prepared::nBits;

			if (!get_Bit(k0, iBit))
				// interleaving is not needed
				continue;

			// set negative bits
			xor_Bit(k0, iBit - Prepared::nBits);
			xor_Bit(k1, iBit - Prepared::nBits);

			for (uint8_t i = 1; i < Prepared::nBits; i++)
			{
				if (get_Bit(k0, iBit - i))
					xor_Bit(k0, iBit - i);
				else
					xor_Bit(k1, iBit - i);
			}

			xor_Bit(k0, iBit);

			// propagate carry
			while (true)
			{
				if (! ++iBit)
					return true; // carry goes outside

				uint8_t val = get_Bit(k0, iBit);
				xor_Bit(k0, iBit);

				if (!val)
					break;
			}
		}

		return false;
	}


	void MultiMac::Calculate(secp256k1_gej& res, const Prepared* pPrepared, secp256k1_scalar* pK, WNafCursor* pWnaf)
	{
		secp256k1_gej_set_infinity(&res);

		for (Index i = 0; i < m_Prepared * 2; i++)
		{
			bool bCarry = false;

			secp256k1_scalar& k = pK[i];
			if (!(1 & i))
				bCarry = WNafCursor::SplitPosNeg(k, pK[i + 1]);

			WNafCursor& wc = pWnaf[i];
			wc.Reset();

			if (bCarry)
			{
				wc.MoveAfterCarry(k);
				if (!wc.m_iBit)
				{
					assert(!wc.m_iElement);
					wc.m_iElement = WNafCursor::s_HiBit;
				}
			}
			else
				wc.MoveNext(k);
		}
		
		for (uint16_t iBit = ECC_Min::nBits + 1; iBit--; ) // extra bit may be necessary because of interleaving
		{
			if (!secp256k1_gej_is_infinity(&res))
				secp256k1_gej_double_var(&res, &res, nullptr);

			for (Index i = 0; i < m_Prepared * 2; i++)
			{
				WNafCursor& wc = pWnaf[i];
				if (static_cast<uint8_t>(iBit) != wc.m_iBit)
					continue;

				// special case: resolve 256-0 ambiguity
				if ((wc.m_iElement ^ static_cast<uint8_t>(iBit >> 1)) & WNafCursor::s_HiBit)
					continue;

				const Prepared& x = pPrepared[i >> 1];

				secp256k1_ge ge;
				secp256k1_ge_from_storage(&ge, x.m_pPt + (wc.m_iElement & ~WNafCursor::s_HiBit));

				if (1 & i)
					secp256k1_ge_neg(&ge, &ge);

				secp256k1_gej_add_ge_var(&res, &res, &ge, nullptr);

				if (iBit)
					wc.MoveNext(pK[i]);
			}
		}
	}





} // namespace ECC_Min

