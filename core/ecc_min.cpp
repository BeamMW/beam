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
	bool MultiMac::WNaf::Cursor::FindCarry(const secp256k1_scalar& k)
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

	void MultiMac::WNaf::Cursor::MoveAfterCarry(const secp256k1_scalar& k)
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

	void MultiMac::WNaf::Cursor::MoveNext(const secp256k1_scalar& k)
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

	uint8_t MultiMac::WNaf::get_Bit(const secp256k1_scalar& k, uint8_t iBit)
	{
		const uint16_t nWordBits = sizeof(secp256k1_scalar_uint) * 8;
		return 1 & (k.d[iBit / nWordBits] >> (iBit & (nWordBits - 1)));
	}

	void MultiMac::WNaf::xor_Bit(secp256k1_scalar& k, uint8_t iBit)
	{
		const uint16_t nWordBits = sizeof(secp256k1_scalar_uint) * 8;

		secp256k1_scalar_uint& x = k.d[iBit / nWordBits];
		secp256k1_scalar_uint msk = uint8_t(1) << (iBit & (nWordBits - 1));

		x ^= msk;
	}

	bool MultiMac::Scalar::SplitPosNeg()
	{
		memset(m_Neg.d, 0, sizeof(m_Neg.d));

		uint8_t iBit = 0;

		while (true)
		{
			// find nnz bit
			for (; ; iBit++)
			{
				if (iBit >= ECC_Min::nBits - Prepared::nBits)
					return false;

				if (WNaf::get_Bit(m_Pos, iBit))
					break;
			}

			iBit += Prepared::nBits;

			if (!WNaf::get_Bit(m_Pos, iBit))
				// interleaving is not needed
				continue;

			// set negative bits
			WNaf::xor_Bit(m_Pos, iBit - Prepared::nBits);
			WNaf::xor_Bit(m_Neg, iBit - Prepared::nBits);

			for (uint8_t i = 1; i < Prepared::nBits; i++)
			{
				if (WNaf::get_Bit(m_Pos, iBit - i))
					WNaf::xor_Bit(m_Pos, iBit - i);
				else
					WNaf::xor_Bit(m_Neg, iBit - i);
			}

			WNaf::xor_Bit(m_Pos, iBit);

			// propagate carry
			while (true)
			{
				if (! ++iBit)
					return true; // carry goes outside

				uint8_t val = WNaf::get_Bit(m_Pos, iBit);
				WNaf::xor_Bit(m_Pos, iBit);

				if (!val)
					break;
			}
		}

		return false;
	}

	void MultiMac::Context::Calculate() const
	{
		secp256k1_gej_set_infinity(m_pRes);

		for (unsigned int i = 0; i < m_Count; i++)
		{
			WNaf& wnaf = m_pWnaf[i];
			wnaf.m_Pos.m_iBit = 0;
			wnaf.m_Neg.m_iBit = 0;

			Scalar& s = m_pS[i];

			bool bCarry = s.SplitPosNeg();
			if (bCarry)
			{
				wnaf.m_Pos.MoveAfterCarry(s.m_Pos);
				if (!wnaf.m_Pos.m_iBit)
				{
					assert(!wnaf.m_Pos.m_iElement);
					wnaf.m_Pos.m_iElement = WNaf::Cursor::s_HiBit;
				}
			}
			else
				wnaf.m_Pos.MoveNext(s.m_Pos);

			wnaf.m_Neg.MoveNext(s.m_Neg);
		}
		
		for (uint16_t iBit = ECC_Min::nBits + 1; iBit--; ) // extra bit may be necessary because of interleaving
		{
			if (!secp256k1_gej_is_infinity(m_pRes))
				secp256k1_gej_double_var(m_pRes, m_pRes, nullptr);

			for (unsigned int i = 0; i < m_Count; i++)
			{
				Process(iBit, i, false);
				Process(iBit, i, true);
			}
		}
	}

	void MultiMac::Context::Process(uint16_t iBit, unsigned int i, bool bNeg) const
	{
		WNaf& wnaf = m_pWnaf[i];
		WNaf::Cursor& wc = bNeg ? wnaf.m_Neg : wnaf.m_Pos;

		if (static_cast<uint8_t>(iBit) != wc.m_iBit)
			return;

		// special case: resolve 256-0 ambiguity
		if ((wc.m_iElement ^ static_cast<uint8_t>(iBit >> 1)) & WNaf::Cursor::s_HiBit)
			return;

		secp256k1_ge ge;
		secp256k1_ge_from_storage(&ge, m_pPrep[i].m_pPt + (wc.m_iElement & ~WNaf::Cursor::s_HiBit));

		if (bNeg)
			secp256k1_ge_neg(&ge, &ge);

		secp256k1_gej_add_ge_var(m_pRes, m_pRes, &ge, nullptr);

		if (iBit)
		{
			const Scalar& s = m_pS[i];
			wc.MoveNext(bNeg ? s.m_Neg : s.m_Pos);
		}
	}



} // namespace ECC_Min

