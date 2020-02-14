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

static const uint32_t ECC_Min_nBits = sizeof(secp256k1_scalar) * 8;

static const uint8_t s_WNaf_HiBit = 0x80;
static_assert(ECC_Min_MultiMac_Prepared_nCount < s_WNaf_HiBit);

#ifdef USE_SCALAR_4X64
typedef uint64_t secp256k1_scalar_uint;
#else // USE_SCALAR_4X64
typedef uint32_t secp256k1_scalar_uint;
#endif // USE_SCALAR_4X64

struct BitWalker
{
	int m_Word;
	secp256k1_scalar_uint m_Msk;
};

inline static void BitWalker_SetPos(BitWalker* p, uint8_t iBit)
{
	const unsigned int nWordBits = sizeof(secp256k1_scalar_uint) * 8;

	p->m_Word = iBit / nWordBits;
	p->m_Msk = secp256k1_scalar_uint(1) << (iBit & (nWordBits - 1));
}

inline static void BitWalker_MoveUp(BitWalker* p)
{
	if (!(p->m_Msk <<= 1))
	{
		p->m_Msk = 1;
		p->m_Word++;
	}
}

inline static void BitWalker_MoveDown(BitWalker* p)
{
	if (!(p->m_Msk >>= 1))
	{
		const unsigned int nWordBits = sizeof(secp256k1_scalar_uint) * 8;
		p->m_Msk = secp256k1_scalar_uint(1) << (nWordBits - 1);

		p->m_Word--;
	}
}

inline static secp256k1_scalar_uint BitWalker_get(const BitWalker* p, const secp256k1_scalar* pK)
{
	return pK->d[p->m_Word] & p->m_Msk;
}

inline static void BitWalker_xor(const BitWalker* p, secp256k1_scalar* pK)
{
	pK->d[p->m_Word] ^= p->m_Msk;
}


void WNaf_Cursor_MoveNext(ECC_Min_MultiMac_WNaf_Cursor* p, const secp256k1_scalar* pK)
{
	BitWalker bw;
	BitWalker_SetPos(&bw, --p->m_iBit);

	// find next nnz bit
	for (; ; p->m_iBit--, BitWalker_MoveDown(&bw))
	{
		if (BitWalker_get(&bw, pK))
			break;

		if (!p->m_iBit)
		{
			// end
			p->m_iBit = 1;
			p->m_iElement = s_WNaf_HiBit;
			return;
		}
	}

	uint8_t nOdd = 1;

	uint8_t nWndBits = ECC_Min_MultiMac_Prepared_nBits - 1;
	if (nWndBits > p->m_iBit)
		nWndBits = p->m_iBit;

	for (uint8_t i = 0; i < nWndBits; i++, p->m_iBit--)
	{
		BitWalker_MoveDown(&bw);
		nOdd = (nOdd << 1) | (BitWalker_get(&bw, pK) != 0);
	}

	for (; !(1 & nOdd); p->m_iBit++)
		nOdd >>= 1;

	p->m_iElement = nOdd >> 1;
}

namespace ECC_Min {

	bool MultiMac::Scalar::SplitPosNeg()
	{
#if ECC_Min_MultiMac_Directions != 2
		static_assert(ECC_Min_MultiMac_Directions == 1);
#else // ECC_Min_MultiMac_Directions

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
				if (iBit >= ECC_Min_nBits - ECC_Min_MultiMac_Prepared_nBits)
					return false;

				if (BitWalker_get(&bw, m_pK))
					break;

				iBit++;
				BitWalker_MoveUp(&bw);
			}

			BitWalker bw0 = bw;

			iBit += ECC_Min_MultiMac_Prepared_nBits;
			for (uint32_t i = 0; i < ECC_Min_MultiMac_Prepared_nBits; i++)
				BitWalker_MoveUp(&bw); // akward

			if (!BitWalker_get(&bw, m_pK))
				continue; // interleaving is not needed

			// set negative bits
			BitWalker_xor(&bw0, m_pK);
			BitWalker_xor(&bw0, m_pK + 1);

			for (uint8_t i = 1; i < ECC_Min_MultiMac_Prepared_nBits; i++)
			{
				BitWalker_MoveUp(&bw0);

				secp256k1_scalar_uint val = BitWalker_get(&bw0, m_pK);
				BitWalker_xor(&bw0, m_pK + !val);
			}

			// propagate carry
			while (true)
			{
				BitWalker_xor(&bw, m_pK);
				if (BitWalker_get(&bw, m_pK))
					break;

				if (! ++iBit)
					return true; // carry goes outside

				BitWalker_MoveUp(&bw);
			}
		}
#endif // ECC_Min_MultiMac_Directions

		return false;
	}

	void MultiMac::Context::Calculate() const
	{
		secp256k1_gej_set_infinity(m_pRes);

		for (unsigned int i = 0; i < m_Count; i++)
		{
			ECC_Min_MultiMac_WNaf& wnaf = m_pWnaf[i];
			Scalar& s = m_pS[i];

			wnaf.m_pC[0].m_iBit = 0;


			bool bCarry = s.SplitPosNeg();
			if (bCarry)
				wnaf.m_pC[0].m_iElement = s_WNaf_HiBit;
			else
				WNaf_Cursor_MoveNext(wnaf.m_pC, s.m_pK);

#if ECC_Min_MultiMac_Directions == 2
			wnaf.m_pC[1].m_iBit = 0;
			WNaf_Cursor_MoveNext(wnaf.m_pC + 1, s.m_pK + 1);
#endif // ECC_Min_MultiMac_Directions
		}
		
		for (uint16_t iBit = ECC_Min_nBits + 1; iBit--; ) // extra bit may be necessary because of interleaving
		{
			if (!secp256k1_gej_is_infinity(m_pRes))
				secp256k1_gej_double_var(m_pRes, m_pRes, nullptr);

			for (unsigned int i = 0; i < m_Count; i++)
			{
				ECC_Min_MultiMac_WNaf& wnaf = m_pWnaf[i];

				for (unsigned int j = 0; j < ECC_Min_MultiMac_Directions; j++)
				{
					ECC_Min_MultiMac_WNaf_Cursor& wc = wnaf.m_pC[j];

					if (static_cast<uint8_t>(iBit) != wc.m_iBit)
						continue;

					// special case: resolve 256-0 ambiguity
					if ((wc.m_iElement ^ static_cast<uint8_t>(iBit >> 1)) & s_WNaf_HiBit)
						continue;

					secp256k1_ge ge;
					secp256k1_ge_from_storage(&ge, m_pPrep[i].m_pPt + (wc.m_iElement & ~s_WNaf_HiBit));

					if (j)
						secp256k1_ge_neg(&ge, &ge);

					secp256k1_gej_add_ge_var(m_pRes, m_pRes, &ge, nullptr);

					if (iBit)
						WNaf_Cursor_MoveNext(&wc, m_pS[i].m_pK + j);
				}
			}
		}
	}


} // namespace ECC_Min

