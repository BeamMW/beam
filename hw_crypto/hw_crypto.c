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

#include <assert.h>
#include "multimac.h"
#include "oracle.h"
#include "noncegen.h"
#include "coinid.h"
#include "kdf.h"
#include "rangeproof.h"
#include "sign.h"
#include "keykeeper.h"

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wunused-function"
#else
#	pragma warning (push, 0) // suppress warnings from secp256k1
#	pragma warning (disable: 4706 4701) // assignment within conditional expression
#endif


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

#define SECURE_ERASE_OBJ(x) BeamCrypto_SecureEraseMem(&x, sizeof(x))

#define s_WNaf_HiBit 0x80
static_assert(BeamCrypto_MultiMac_Fast_nCount < s_WNaf_HiBit, "");

#ifdef USE_SCALAR_4X64
typedef uint64_t secp256k1_scalar_uint;
#else // USE_SCALAR_4X64
typedef uint32_t secp256k1_scalar_uint;
#endif // USE_SCALAR_4X64

#ifndef _countof
#	define _countof(arr) sizeof(arr) / sizeof((arr)[0])
#endif

#define secp256k1_scalar_WordBits (sizeof(secp256k1_scalar_uint) * 8)

//////////////////////////////
// MultiMac
typedef struct
{
	int m_Word;
	secp256k1_scalar_uint m_Msk;
} BitWalker;

inline static void BitWalker_SetPos(BitWalker* p, uint8_t iBit)
{
	p->m_Word = iBit / secp256k1_scalar_WordBits;
	p->m_Msk = ((secp256k1_scalar_uint) 1) << (iBit & (secp256k1_scalar_WordBits - 1));
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
		p->m_Msk = ((secp256k1_scalar_uint) 1) << (secp256k1_scalar_WordBits - 1);

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


static void WNaf_Cursor_MoveNext(BeamCrypto_MultiMac_WNaf_Cursor* p, const secp256k1_scalar* pK)
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

	uint8_t nWndBits = BeamCrypto_MultiMac_Fast_nBits - 1;
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

static int Scalar_SplitPosNeg(BeamCrypto_MultiMac_Scalar* p)
{
#if BeamCrypto_MultiMac_Directions != 2
	static_assert(BeamCrypto_MultiMac_Directions == 1, "");
#else // BeamCrypto_MultiMac_Directions

	memset(p->m_pK[1].d, 0, sizeof(p->m_pK[1].d));

	uint8_t iBit = 0;
	BitWalker bw;
	bw.m_Word = 0;
	bw.m_Msk = 1;

	while (1)
	{
		// find nnz bit
		while (1)
		{
			if (iBit >= BeamCrypto_nBits - BeamCrypto_MultiMac_Fast_nBits)
				return 0;

			if (BitWalker_get(&bw, p->m_pK))
				break;

			iBit++;
			BitWalker_MoveUp(&bw);
		}

		BitWalker bw0 = bw;

		iBit += BeamCrypto_MultiMac_Fast_nBits;
		for (uint32_t i = 0; i < BeamCrypto_MultiMac_Fast_nBits; i++)
			BitWalker_MoveUp(&bw); // akward

		if (!BitWalker_get(&bw, p->m_pK))
			continue; // interleaving is not needed

		// set negative bits
		BitWalker_xor(&bw0, p->m_pK);
		BitWalker_xor(&bw0, p->m_pK + 1);

		for (uint8_t i = 1; i < BeamCrypto_MultiMac_Fast_nBits; i++)
		{
			BitWalker_MoveUp(&bw0);

			secp256k1_scalar_uint val = BitWalker_get(&bw0, p->m_pK);
			BitWalker_xor(&bw0, p->m_pK + !val);
		}

		// propagate carry
		while (1)
		{
			BitWalker_xor(&bw, p->m_pK);
			if (BitWalker_get(&bw, p->m_pK))
				break;

			if (! ++iBit)
				return 1; // carry goes outside

			BitWalker_MoveUp(&bw);
		}
	}
#endif // BeamCrypto_MultiMac_Directions

	return 0;
}

void mem_cmov(unsigned int* pDst, const unsigned int* pSrc, int flag, unsigned int nWords)
{
	const unsigned int mask0 = flag + ~((unsigned int) 0);
	const unsigned int mask1 = ~mask0;

	for (unsigned int n = 0; n < nWords; n++)
		pDst[n] = (pDst[n] & mask0) | (pSrc[n] & mask1);
}

void BeamCrypto_MultiMac_Calculate(const BeamCrypto_MultiMac_Context* p)
{
	secp256k1_gej_set_infinity(p->m_pRes);

	for (unsigned int i = 0; i < p->m_Fast; i++)
	{
		BeamCrypto_MultiMac_WNaf* pWnaf = p->m_pWnaf + i;
		BeamCrypto_MultiMac_Scalar* pS = p->m_pS + i;

		pWnaf->m_pC[0].m_iBit = 0;

		int carry = Scalar_SplitPosNeg(pS);
		if (carry)
			pWnaf->m_pC[0].m_iElement = s_WNaf_HiBit;
		else
			WNaf_Cursor_MoveNext(pWnaf->m_pC, pS->m_pK);

#if BeamCrypto_MultiMac_Directions == 2
		pWnaf->m_pC[1].m_iBit = 0;
		WNaf_Cursor_MoveNext(pWnaf->m_pC + 1, pS->m_pK + 1);
#endif // BeamCrypto_MultiMac_Directions
	}

	secp256k1_ge ge;
	secp256k1_ge_storage ges;

	for (uint16_t iBit = BeamCrypto_nBits + 1; iBit--; ) // extra bit may be necessary because of interleaving
	{
		secp256k1_gej_double_var(p->m_pRes, p->m_pRes, 0); // would be fast if zero, no need to check explicitly

		if (!(iBit % BeamCrypto_MultiMac_Secure_nBits) && (iBit < BeamCrypto_nBits) && p->m_Secure)
		{
			static_assert(!(secp256k1_scalar_WordBits % BeamCrypto_MultiMac_Secure_nBits), "");

			unsigned int iWord = iBit / secp256k1_scalar_WordBits;
			unsigned int nShift = iBit % secp256k1_scalar_WordBits;
			const secp256k1_scalar_uint nMsk = ((1U << BeamCrypto_MultiMac_Secure_nBits) - 1);

			for (unsigned int i = 0; i < p->m_Secure; i++)
			{
				unsigned int iElement = (p->m_pSecureK[i].d[iWord] >> nShift) & nMsk;
				const BeamCrypto_MultiMac_Secure* pGen = p->m_pGenSecure + i;

				for (unsigned int j = 0; j < BeamCrypto_MultiMac_Secure_nCount; j++)
				{
					static_assert(sizeof(ges) == sizeof(pGen->m_pPt[j]), "");
					static_assert(!(sizeof(ges) % sizeof(unsigned int)), "");

					mem_cmov(
						(unsigned int*) &ges,
						(unsigned int*) (pGen->m_pPt + j),
						iElement == j,
						sizeof(ges) / sizeof(unsigned int));
				}

				secp256k1_ge_from_storage(&ge, &ges);

				if (p->m_pZDenom)
					secp256k1_gej_add_zinv_var(p->m_pRes, p->m_pRes, &ge, p->m_pZDenom);
				else
					secp256k1_gej_add_ge_var(p->m_pRes, p->m_pRes, &ge, 0);
			}
		}

		for (unsigned int i = 0; i < p->m_Fast; i++)
		{
			BeamCrypto_MultiMac_WNaf* pWnaf = p->m_pWnaf + i;

			for (unsigned int j = 0; j < BeamCrypto_MultiMac_Directions; j++)
			{
				BeamCrypto_MultiMac_WNaf_Cursor* pWc = pWnaf->m_pC + j;

				if (((uint8_t) iBit) != pWc->m_iBit)
					continue;

				// special case: resolve 256-0 ambiguity
				if ((pWc->m_iElement ^ ((uint8_t) (iBit >> 1))) & s_WNaf_HiBit)
					continue;

				secp256k1_ge_from_storage(&ge, p->m_pGenFast[i].m_pPt + (pWc->m_iElement & ~s_WNaf_HiBit));

				if (j)
					secp256k1_ge_neg(&ge, &ge);

				secp256k1_gej_add_ge_var(p->m_pRes, p->m_pRes, &ge, 0);

				if (iBit)
					WNaf_Cursor_MoveNext(pWc, p->m_pS[i].m_pK + j);
			}
		}
	}

	SECURE_ERASE_OBJ(ges);

	if (p->m_pZDenom)
		// fix denominator
		secp256k1_fe_mul(&p->m_pRes->z, &p->m_pRes->z, p->m_pZDenom);

	for (unsigned int i = 0; i < p->m_Secure; i++)
	{
		secp256k1_ge_from_storage(&ge, p->m_pGenSecure[i].m_pPt + BeamCrypto_MultiMac_Secure_nCount);
		secp256k1_gej_add_ge_var(p->m_pRes, p->m_pRes, &ge, 0);
	}
}

//////////////////////////////
// Batch normalization
static void secp256k1_gej_rescale_XY(secp256k1_gej* pGej, const secp256k1_fe* pZ)
{
	// equivalent of secp256k1_gej_rescale, but doesn't change z coordinate
	// A bit more effective when the value of z is known in advance (such as when normalizing)
	secp256k1_fe zz;
	secp256k1_fe_sqr(&zz, pZ);

	secp256k1_fe_mul(&pGej->x, &pGej->x, &zz);
	secp256k1_fe_mul(&pGej->y, &pGej->y, &zz);
	secp256k1_fe_mul(&pGej->y, &pGej->y, pZ);
}

static void BatchNormalize_Fwd(secp256k1_fe* pFe, unsigned int n, const secp256k1_gej* pGej, const secp256k1_fe* pFePrev)
{
	if (n)
		secp256k1_fe_mul(pFe, pFePrev, &pGej->z);
	else
		*pFe = pGej[0].z;
}

static void BatchNormalize_Apex(secp256k1_fe* pZDenom, secp256k1_fe* pFePrev, int nNormalize)
{
	if (nNormalize)
		secp256k1_fe_inv(pZDenom, pFePrev); // the only expensive call
	else
		secp256k1_fe_set_int(pZDenom, 1);
}

static void BatchNormalize_Bwd(secp256k1_fe* pFe, unsigned int n, secp256k1_gej* pGej, const secp256k1_fe* pFePrev, secp256k1_fe* pZDenom)
{
	if (n)
		secp256k1_fe_mul(pFe, pFePrev, pZDenom);
	else
		*pFe = *pZDenom;

	secp256k1_gej_rescale_XY(pGej, pFe);

	secp256k1_fe_mul(pZDenom, pZDenom, &pGej->z);
}


static void BeamCrypto_ToCommonDenominator(unsigned int nCount, secp256k1_gej* pGej, secp256k1_fe* pFe, secp256k1_fe* pZDenom, int nNormalize)
{
	assert(nCount);

	for (unsigned int i = 0; i < nCount; i++)
		BatchNormalize_Fwd(pFe + i, i, pGej + i, pFe + i - 1);

	BatchNormalize_Apex(pZDenom, pFe + nCount - 1, nNormalize);

	for (unsigned int i = nCount; i--; )
		BatchNormalize_Bwd(pFe + i, i, pGej + i, pFe + i - 1, pZDenom);
}

static void secp256k1_ge_set_gej_normalized(secp256k1_ge* pGe, const secp256k1_gej* pGej)
{
	pGe->infinity = pGej->infinity;
	pGe->x = pGej->x;
	pGe->y = pGej->y;
}

static void BeamCrypto_MultiMac_SetCustom_Nnz(BeamCrypto_MultiMac_Context* p, BeamCrypto_FlexPoint* pFlex)
{
	assert(p->m_Fast == 1);
	assert(p->m_pZDenom);

	BeamCrypto_FlexPoint_MakeGej(pFlex);
	assert(BeamCrypto_FlexPoint_Gej & pFlex->m_Flags);
	assert(!secp256k1_gej_is_infinity(&pFlex->m_Gej));

	secp256k1_gej pOdds[BeamCrypto_MultiMac_Fast_nCount];
	pOdds[0] = pFlex->m_Gej;

	// calculate odd powers
	secp256k1_gej x2;
	secp256k1_gej_double_var(&x2, pOdds, 0);

	for (unsigned int i = 1; i < BeamCrypto_MultiMac_Fast_nCount; i++)
	{
		secp256k1_gej_add_var(pOdds + i, pOdds + i - 1, &x2, 0);
		assert(!secp256k1_gej_is_infinity(pOdds + i)); // odd powers of non-zero point must not be zero!
	}

	secp256k1_fe pFe[BeamCrypto_MultiMac_Fast_nCount];

	BeamCrypto_ToCommonDenominator(BeamCrypto_MultiMac_Fast_nCount, pOdds, pFe, p->m_pZDenom, 0);

	for (unsigned int i = 0; i < BeamCrypto_MultiMac_Fast_nCount; i++)
	{
		secp256k1_ge ge;
		secp256k1_ge_set_gej_normalized(&ge, pOdds + i);
		secp256k1_ge_to_storage((secp256k1_ge_storage*)p->m_pGenFast[0].m_pPt + i, &ge);
	}
}

//////////////////////////////
// NonceGenerator
void BeamCrypto_NonceGenerator_InitBegin(BeamCrypto_NonceGenerator* p, secp256k1_hmac_sha256_t* pHMac, const char* szSalt, size_t nSalt)
{
	p->m_Counter = 0;
	p->m_FirstTime = 1;
	p->m_pContext = 0;
	p->m_nContext = 0;

	secp256k1_hmac_sha256_initialize(pHMac, (uint8_t*) szSalt, nSalt);
}

void BeamCrypto_NonceGenerator_InitEnd(BeamCrypto_NonceGenerator* p, secp256k1_hmac_sha256_t* pHMac)
{
	secp256k1_hmac_sha256_finalize(pHMac, p->m_Prk.m_pVal);
}

void BeamCrypto_NonceGenerator_Init(BeamCrypto_NonceGenerator* p, const char* szSalt, size_t nSalt, const BeamCrypto_UintBig* pSeed)
{
	secp256k1_hmac_sha256_t hmac;

	BeamCrypto_NonceGenerator_InitBegin(p, &hmac, szSalt, nSalt);
	secp256k1_hmac_sha256_write(&hmac, pSeed->m_pVal, sizeof(pSeed->m_pVal));
	BeamCrypto_NonceGenerator_InitEnd(p, &hmac);
}

void BeamCrypto_NonceGenerator_NextOkm(BeamCrypto_NonceGenerator* p)
{
	// Expand
	secp256k1_hmac_sha256_t hmac;
	secp256k1_hmac_sha256_initialize(&hmac, p->m_Prk.m_pVal, sizeof(p->m_Prk.m_pVal));

	if (p->m_FirstTime)
		p->m_FirstTime = 0;
	else
		secp256k1_hmac_sha256_write(&hmac, p->m_Okm.m_pVal, sizeof(p->m_Okm.m_pVal));

	secp256k1_hmac_sha256_write(&hmac, p->m_pContext, p->m_nContext);

	p->m_Counter++;
	secp256k1_hmac_sha256_write(&hmac, &p->m_Counter, sizeof(p->m_Counter));

	secp256k1_hmac_sha256_finalize(&hmac, p->m_Okm.m_pVal);
}

static int ScalarImportNnz(secp256k1_scalar* pS, const uint8_t* p)
{
	int overflow;
	secp256k1_scalar_set_b32(pS, p, &overflow);

	return !(overflow || secp256k1_scalar_is_zero(pS));
}

void BeamCrypto_NonceGenerator_NextScalar(BeamCrypto_NonceGenerator* p, secp256k1_scalar* pS)
{
	while (1)
	{
		BeamCrypto_NonceGenerator_NextOkm(p);
		if (ScalarImportNnz(pS, p->m_Okm.m_pVal))
			break;
	}
}

static int IsUintBigZero(const BeamCrypto_UintBig* p)
{
	// const-time isn't required
	for (unsigned int i = 0; i < _countof(p->m_pVal); i++)
		if (p->m_pVal[i])
			return 0;
	return 1;
}

//////////////////////////////
// Point
void BeamCrypto_FlexPoint_MakeCompact(BeamCrypto_FlexPoint* pFlex)
{
	if ((BeamCrypto_FlexPoint_Compact & pFlex->m_Flags) || !pFlex->m_Flags)
		return;

	BeamCrypto_FlexPoint_MakeGe(pFlex);
	assert(BeamCrypto_FlexPoint_Ge & pFlex->m_Flags);

	if (secp256k1_ge_is_infinity(&pFlex->m_Ge))
		memset(&pFlex->m_Compact, 0, sizeof(pFlex->m_Compact));
	else
	{
		secp256k1_fe_normalize(&pFlex->m_Ge.x);
		secp256k1_fe_normalize(&pFlex->m_Ge.y);

		secp256k1_fe_get_b32(pFlex->m_Compact.m_X.m_pVal, &pFlex->m_Ge.x);
		pFlex->m_Compact.m_Y = (secp256k1_fe_is_odd(&pFlex->m_Ge.y) != 0);
	}

	pFlex->m_Flags |= BeamCrypto_FlexPoint_Compact;
}

void BeamCrypto_FlexPoint_MakeGej(BeamCrypto_FlexPoint* pFlex)
{
	if (BeamCrypto_FlexPoint_Gej & pFlex->m_Flags)
		return;

	BeamCrypto_FlexPoint_MakeGe(pFlex);
	if (!pFlex->m_Flags)
		return;

	assert(BeamCrypto_FlexPoint_Ge & pFlex->m_Flags);
	secp256k1_gej_set_ge(&pFlex->m_Gej, &pFlex->m_Ge);

	pFlex->m_Flags |= BeamCrypto_FlexPoint_Gej;
}

void BeamCrypto_FlexPoint_MakeGe(BeamCrypto_FlexPoint* pFlex)
{
	if (BeamCrypto_FlexPoint_Ge & pFlex->m_Flags)
		return;

	if (BeamCrypto_FlexPoint_Gej & pFlex->m_Flags)
		secp256k1_ge_set_gej_var(&pFlex->m_Ge, &pFlex->m_Gej); // expensive, better to a batch convertion
	else
	{
		if (!(BeamCrypto_FlexPoint_Compact & pFlex->m_Flags))
			return;

		pFlex->m_Flags = 0; // will restore Compact flag iff import is successful

		if (pFlex->m_Compact.m_Y > 1)
			return; // not well-formed

		secp256k1_fe nx;
		if (!secp256k1_fe_set_b32(&nx, pFlex->m_Compact.m_X.m_pVal))
			return; // not well-formed

		if (!secp256k1_ge_set_xo_var(&pFlex->m_Ge, &nx, pFlex->m_Compact.m_Y))
		{
			// be convention zeroed Compact is a zero point
			if (pFlex->m_Compact.m_Y || !IsUintBigZero(&pFlex->m_Compact.m_X))
				return;

			pFlex->m_Ge.infinity = 1; // no specific function like secp256k1_ge_set_infinity
		}

		pFlex->m_Flags = BeamCrypto_FlexPoint_Compact; // restored
	}

	pFlex->m_Flags |= BeamCrypto_FlexPoint_Ge;
}

void BeamCrypto_FlexPoint_MakeGe_Batch(BeamCrypto_FlexPoint* pFlex, unsigned int nCount)
{
	assert(nCount);

	static_assert(sizeof(pFlex->m_Ge) >= sizeof(secp256k1_fe), "Ge is used as a temp placeholder for Fe");
#define FLEX_POINT_TEMP_FE(pt) ((secp256k1_fe*) (&(pt).m_Ge))

	for (unsigned int i = 0; i < nCount; i++)
	{
		assert(BeamCrypto_FlexPoint_Gej == pFlex[i].m_Flags);

		BatchNormalize_Fwd(FLEX_POINT_TEMP_FE(pFlex[i]), i, &pFlex[i].m_Gej, FLEX_POINT_TEMP_FE(pFlex[i - 1]));
	}

	secp256k1_fe zDenom;
	BatchNormalize_Apex(&zDenom, FLEX_POINT_TEMP_FE(pFlex[nCount - 1]), 1);

	for (unsigned int i = nCount; i--; )
	{
		BatchNormalize_Bwd(FLEX_POINT_TEMP_FE(pFlex[i]), i, &pFlex[i].m_Gej, FLEX_POINT_TEMP_FE(pFlex[i - 1]), &zDenom);

		secp256k1_ge_set_gej_normalized(&pFlex[i].m_Ge, &pFlex[i].m_Gej);
		pFlex[i].m_Flags = BeamCrypto_FlexPoint_Ge;
	}

	assert(nCount);
}

void BeamCrypto_MulPoint(BeamCrypto_FlexPoint* pFlex, const BeamCrypto_MultiMac_Secure* pGen, const secp256k1_scalar* pK)
{
	BeamCrypto_MultiMac_Context ctx;
	ctx.m_pRes = &pFlex->m_Gej;
	ctx.m_pZDenom = 0;
	ctx.m_Fast = 0;
	ctx.m_Secure = 1;
	ctx.m_pGenSecure = pGen;
	ctx.m_pSecureK = pK;

	BeamCrypto_MultiMac_Calculate(&ctx);
	pFlex->m_Flags = BeamCrypto_FlexPoint_Gej;
}

void BeamCrypto_MulG(BeamCrypto_FlexPoint* pFlex, const secp256k1_scalar* pK)
{
	BeamCrypto_MulPoint(pFlex, BeamCrypto_Context_get()->m_pGenGJ, pK);
}

void BeamCrypto_Sk2Pk(BeamCrypto_UintBig* pRes, secp256k1_scalar* pK)
{
	BeamCrypto_FlexPoint fp;
	BeamCrypto_MulG(&fp, pK);

	BeamCrypto_FlexPoint_MakeCompact(&fp);
	assert(BeamCrypto_FlexPoint_Compact & fp.m_Flags);

	*pRes = fp.m_Compact.m_X;

	if (fp.m_Compact.m_Y)
		secp256k1_scalar_negate(pK, pK);
}
//////////////////////////////
// Oracle
void BeamCrypto_Oracle_Init(BeamCrypto_Oracle* p)
{
	secp256k1_sha256_initialize(&p->m_sha);
}

void BeamCrypto_Oracle_Expose(BeamCrypto_Oracle* p, const uint8_t* pPtr, size_t nSize)
{
	secp256k1_sha256_write(&p->m_sha, pPtr, nSize);
}

void BeamCrypto_Oracle_NextHash(BeamCrypto_Oracle* p, BeamCrypto_UintBig* pHash)
{
	secp256k1_sha256_t sha = p->m_sha; // copy
	secp256k1_sha256_finalize(&sha, pHash->m_pVal);

	secp256k1_sha256_write(&p->m_sha, pHash->m_pVal, BeamCrypto_nBytes);
}

void BeamCrypto_Oracle_NextScalar(BeamCrypto_Oracle* p, secp256k1_scalar* pS)
{
	while (1)
	{
		BeamCrypto_UintBig hash;
		BeamCrypto_Oracle_NextHash(p, &hash);

		if (ScalarImportNnz(pS, hash.m_pVal))
			break;
	}
}

void BeamCrypto_Oracle_NextPoint(BeamCrypto_Oracle* p, BeamCrypto_FlexPoint* pFlex)
{
	pFlex->m_Compact.m_Y = 0;

	while (1)
	{
		BeamCrypto_Oracle_NextHash(p, &pFlex->m_Compact.m_X);
		pFlex->m_Flags = BeamCrypto_FlexPoint_Compact;

		BeamCrypto_FlexPoint_MakeGe(pFlex);

		if ((BeamCrypto_FlexPoint_Ge & pFlex->m_Flags) && !secp256k1_ge_is_infinity(&pFlex->m_Ge))
			break;
	}
}

//////////////////////////////
// CoinID
#define BeamCrypto_CoinID_nSubkeyBits 24

int BeamCrypto_CoinID_getSchemeAndSubkey(const BeamCrypto_CoinID* p, uint8_t* pScheme, uint32_t* pSubkey)
{
	*pScheme = (uint8_t) (p->m_SubIdx >> BeamCrypto_CoinID_nSubkeyBits);
	*pSubkey = p->m_SubIdx & ((1U << BeamCrypto_CoinID_nSubkeyBits) - 1);

	if (!*pSubkey)
		return 0; // by convention: up to latest scheme, Subkey=0 - is a master key

	if (BeamCrypto_CoinID_Scheme_BB21 == *pScheme)
		return 0; // BB2.1 workaround

	return 1;
}

#define HASH_WRITE_STR(hash, str) secp256k1_sha256_write(&(hash), str, sizeof(str))

void secp256k1_sha256_write_Num(secp256k1_sha256_t* pSha, uint64_t val)
{
	int nContinue;
	do
	{
		uint8_t x = (uint8_t)val;

		nContinue = (val >= 0x80);
		if (nContinue)
		{
			x |= 0x80;
			val >>= 7;
		}

		secp256k1_sha256_write(pSha, &x, sizeof(x));

	} while (nContinue);
}

void secp256k1_sha256_write_CompactPoint(secp256k1_sha256_t* pSha, const BeamCrypto_CompactPoint* pCompact)
{
	secp256k1_sha256_write(pSha, pCompact->m_X.m_pVal, sizeof(pCompact->m_X.m_pVal));
	secp256k1_sha256_write(pSha, &pCompact->m_Y, sizeof(pCompact->m_Y));
}

void secp256k1_sha256_write_Point(secp256k1_sha256_t* pSha, BeamCrypto_FlexPoint* pFlex)
{
	BeamCrypto_FlexPoint_MakeCompact(pFlex);
	assert(BeamCrypto_FlexPoint_Compact & pFlex->m_Flags);
	secp256k1_sha256_write_CompactPoint(pSha, &pFlex->m_Compact);
}

void BeamCrypto_CoinID_getHash(const BeamCrypto_CoinID* p, BeamCrypto_UintBig* pHash)
{
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);

	uint8_t nScheme;
	uint32_t nSubkey;
	BeamCrypto_CoinID_getSchemeAndSubkey(p, &nScheme, &nSubkey);

	uint32_t nSubIdx = p->m_SubIdx;

	switch (nScheme)
	{
	case BeamCrypto_CoinID_Scheme_BB21:
		// this is actually V0, with a workaround
		nSubIdx = nSubkey | (BeamCrypto_CoinID_Scheme_V0 << BeamCrypto_CoinID_nSubkeyBits);
		nScheme = BeamCrypto_CoinID_Scheme_V0;
		// no break;

	case BeamCrypto_CoinID_Scheme_V0:
		HASH_WRITE_STR(sha, "kid");
		break;

	default:
		HASH_WRITE_STR(sha, "kidv-1");
	}

	secp256k1_sha256_write_Num(&sha, p->m_Idx);
	secp256k1_sha256_write_Num(&sha, p->m_Type);
	secp256k1_sha256_write_Num(&sha, nSubIdx);

	if (nScheme >= BeamCrypto_CoinID_Scheme_V1)
	{
		// newer scheme - account for the Value and Asset.
		secp256k1_sha256_write_Num(&sha, p->m_Amount);

		if (p->m_AssetID)
		{
			HASH_WRITE_STR(sha, "asset");
			secp256k1_sha256_write_Num(&sha, p->m_AssetID);
		}
	}

	secp256k1_sha256_finalize(&sha, pHash->m_pVal);
}

//////////////////////////////
// Kdf
void BeamCrypto_Kdf_Init(BeamCrypto_Kdf* p, const BeamCrypto_UintBig* pSeed)
{
	static const char szSalt[] = "beam-HKdf";

	BeamCrypto_NonceGenerator ng1, ng2;
	BeamCrypto_NonceGenerator_Init(&ng1, szSalt, sizeof(szSalt), pSeed);
	ng2 = ng1;

	static const char szCtx1[] = "gen";
	static const char szCtx2[] = "coF";

	ng1.m_pContext = szCtx1;
	ng1.m_nContext = sizeof(szCtx1);

	BeamCrypto_NonceGenerator_NextOkm(&ng1);
	p->m_Secret = ng1.m_Okm;

	ng2.m_pContext = szCtx2;
	ng2.m_nContext = sizeof(szCtx2);
	BeamCrypto_NonceGenerator_NextScalar(&ng2, &p->m_kCoFactor);

	SECURE_ERASE_OBJ(ng1);
	SECURE_ERASE_OBJ(ng2);
}

void BeamCrypto_Kdf_Derive_PKey(const BeamCrypto_Kdf* p, const BeamCrypto_UintBig* pHv, secp256k1_scalar* pK)
{
	static const char szSalt[] = "beam-Key";

	BeamCrypto_NonceGenerator ng;
	secp256k1_hmac_sha256_t hmac;
	BeamCrypto_NonceGenerator_InitBegin(&ng, &hmac, szSalt, sizeof(szSalt));

	secp256k1_hmac_sha256_write(&hmac, p->m_Secret.m_pVal, sizeof(p->m_Secret.m_pVal));
	secp256k1_hmac_sha256_write(&hmac, pHv->m_pVal, sizeof(pHv->m_pVal));

	BeamCrypto_NonceGenerator_InitEnd(&ng, &hmac);

	BeamCrypto_NonceGenerator_NextScalar(&ng, pK);

	SECURE_ERASE_OBJ(ng);
}

void BeamCrypto_Kdf_Derive_SKey(const BeamCrypto_Kdf* p, const BeamCrypto_UintBig* pHv, secp256k1_scalar* pK)
{
	BeamCrypto_Kdf_Derive_PKey(p, pHv, pK);
	secp256k1_scalar_mul(pK, pK, &p->m_kCoFactor);
}

#define ARRAY_ELEMENT_SAFE(arr, index) ((arr)[(((index) < _countof(arr)) ? (index) : (_countof(arr) - 1))])
#define FOURCC_FROM_BYTES(a, b, c, d) (((((((uint32_t) a << 8) | (uint32_t) b) << 8) | (uint32_t) c) << 8) | (uint32_t) d)
#define FOURCC_FROM_STR(name) FOURCC_FROM_BYTES(ARRAY_ELEMENT_SAFE(#name,0), ARRAY_ELEMENT_SAFE(#name,1), ARRAY_ELEMENT_SAFE(#name,2), ARRAY_ELEMENT_SAFE(#name,3))

void BeamCrypto_Kdf_getChild(BeamCrypto_Kdf* p, uint32_t iChild, const BeamCrypto_Kdf* pParent)
{
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);
	HASH_WRITE_STR(sha, "kid");

	const uint32_t nType = FOURCC_FROM_STR(SubK);

	secp256k1_sha256_write_Num(&sha, iChild);
	secp256k1_sha256_write_Num(&sha, nType);
	secp256k1_sha256_write_Num(&sha, 0);

	BeamCrypto_UintBig hv;
	secp256k1_sha256_finalize(&sha, hv.m_pVal);

	secp256k1_scalar sk;
	BeamCrypto_Kdf_Derive_SKey(pParent, &hv, &sk);

	secp256k1_scalar_get_b32(hv.m_pVal, &sk);
	SECURE_ERASE_OBJ(sk);

	BeamCrypto_Kdf_Init(p, &hv);
	SECURE_ERASE_OBJ(hv);
}

//////////////////////////////
// Kdf - CoinID key derivation
void BeamCrypto_CoinID_getSk(const BeamCrypto_Kdf* pKdf, const BeamCrypto_CoinID* pCid, secp256k1_scalar* pK)
{
	BeamCrypto_CoinID_getSkComm(pKdf, pCid, pK, 0);
}

void BeamCrypto_CoinID_getSkComm(const BeamCrypto_Kdf* pKdf, const BeamCrypto_CoinID* pCid, secp256k1_scalar* pK, BeamCrypto_FlexPoint* pComm)
{
	BeamCrypto_FlexPoint pFlex[2];

	union
	{
		// save some space
		struct
		{
			uint8_t nScheme;
			uint32_t nSubkey;
			BeamCrypto_UintBig hv;
			BeamCrypto_Kdf kdfC;
		} k;

		struct
		{
			BeamCrypto_Oracle oracle;
			secp256k1_scalar k1;
		} o;

		struct
		{
			BeamCrypto_MultiMac_Scalar s;
			BeamCrypto_MultiMac_WNaf wnaf;
			BeamCrypto_MultiMac_Fast genAsset;
			secp256k1_fe zDenom;
		} mm;

	} u;

	int nChild = BeamCrypto_CoinID_getSchemeAndSubkey(pCid, &u.k.nScheme, &u.k.nSubkey);
	if (nChild)
	{
		BeamCrypto_Kdf_getChild(&u.k.kdfC, u.k.nSubkey, pKdf);
		pKdf = &u.k.kdfC;
	}

	BeamCrypto_CoinID_getHash(pCid, &u.k.hv);

	BeamCrypto_Kdf_Derive_SKey(pKdf, &u.k.hv, pK);

	if (nChild)
		SECURE_ERASE_OBJ(u.k.kdfC);

	BeamCrypto_Context* pCtx = BeamCrypto_Context_get();


	// sk*G + v*H
	BeamCrypto_MultiMac_Context mmCtx;
	mmCtx.m_pRes = &pFlex[0].m_Gej;
	mmCtx.m_Secure = 1;
	mmCtx.m_pSecureK = pK;
	mmCtx.m_pGenSecure = pCtx->m_pGenGJ;
	mmCtx.m_Fast = 1;
	mmCtx.m_pS = &u.mm.s;
	mmCtx.m_pWnaf = &u.mm.wnaf;

	if (pCid->m_AssetID)
	{
		// derive asset gen
		BeamCrypto_Oracle_Init(&u.o.oracle);

		HASH_WRITE_STR(u.o.oracle.m_sha, "B.Asset.Gen.V1");
		secp256k1_sha256_write_Num(&u.o.oracle.m_sha, pCid->m_AssetID);

		BeamCrypto_FlexPoint fpAsset;
		BeamCrypto_Oracle_NextPoint(&u.o.oracle, &fpAsset);

		mmCtx.m_pGenFast = &u.mm.genAsset;
		mmCtx.m_pZDenom = &u.mm.zDenom;

		BeamCrypto_MultiMac_SetCustom_Nnz(&mmCtx, &fpAsset);

	}
	else
	{
		mmCtx.m_pGenFast = pCtx->m_pGenFast + BeamCrypto_MultiMac_Fast_Idx_H;
		mmCtx.m_pZDenom = 0;
	}

	secp256k1_scalar_set_u64(u.mm.s.m_pK, pCid->m_Amount);

	BeamCrypto_MultiMac_Calculate(&mmCtx);
	pFlex[0].m_Flags = BeamCrypto_FlexPoint_Gej;

	// sk * J
	mmCtx.m_pRes = &pFlex[1].m_Gej;
	mmCtx.m_pGenSecure = pCtx->m_pGenGJ + 1;
	mmCtx.m_pZDenom = 0;
	mmCtx.m_Fast = 0;

	BeamCrypto_MultiMac_Calculate(&mmCtx);
	pFlex[1].m_Flags = BeamCrypto_FlexPoint_Gej;

	// adjust sk
	BeamCrypto_FlexPoint_MakeGe_Batch(pFlex, _countof(pFlex));

	BeamCrypto_Oracle_Init(&u.o.oracle);

	for (unsigned int i = 0; i < _countof(pFlex); i++)
		secp256k1_sha256_write_Point(&u.o.oracle.m_sha, pFlex + i);

	BeamCrypto_Oracle_NextScalar(&u.o.oracle, &u.o.k1);

	secp256k1_scalar_add(pK, pK, &u.o.k1);

	if (pComm)
	{
		mmCtx.m_pGenSecure = pCtx->m_pGenGJ; // not really secure here, just no good reason to have additional non-secure J-gen
		mmCtx.m_pSecureK = &u.o.k1;

		BeamCrypto_MultiMac_Calculate(&mmCtx);
		pFlex[1].m_Flags = BeamCrypto_FlexPoint_Gej;

		assert(BeamCrypto_FlexPoint_Ge & pFlex[0].m_Flags);

		secp256k1_gej_add_ge_var(&pComm->m_Gej, &pFlex[1].m_Gej, &pFlex[0].m_Ge, 0);
		pComm->m_Flags = BeamCrypto_FlexPoint_Gej;
	}
}

static void BeamCrypto_ShieldedInput_getSk(const BeamCrypto_Kdf* pKdf, const BeamCrypto_ShieldedInput* pInp, secp256k1_scalar* pK)
{
	BeamCrypto_UintBig hv;
	secp256k1_sha256_t sha;

	secp256k1_sha256_initialize(&sha);
	HASH_WRITE_STR(sha, "sh.skout");
	secp256k1_sha256_write_Num(&sha, pInp->m_TxoID.m_Amount);
	secp256k1_sha256_write_Num(&sha, pInp->m_TxoID.m_AssetID);
	secp256k1_sha256_write_Num(&sha, pInp->m_Fee);
	secp256k1_sha256_write(&sha, pInp->m_TxoID.m_kSerG.m_pVal, sizeof(pInp->m_TxoID.m_kSerG.m_pVal));
	secp256k1_sha256_write_Num(&sha, !!pInp->m_TxoID.m_IsCreatedByViewer);
	secp256k1_sha256_write_Num(&sha, pInp->m_TxoID.m_nViewerIdx);
	secp256k1_sha256_finalize(&sha, hv.m_pVal);

	BeamCrypto_Kdf_Derive_SKey(pKdf, &hv, pK);
}

//////////////////////////////
// RangeProof

static void WriteInNetworkOrder(uint8_t** ppDst, uint64_t val, unsigned int nLen)
{
	for (unsigned int i = 0; i < nLen; i++, val >>= 8)
	{
		--*ppDst;
		**ppDst = (uint8_t) val;
	}
}

typedef struct
{
	BeamCrypto_RangeProof* m_pRangeProof;
	BeamCrypto_NonceGenerator m_NonceGen; // 88 bytes
	secp256k1_gej m_pGej[2]; // 248 bytes

	// 97 bytes. This can be saved, at expense of calculating them again (BeamCrypto_CoinID_getSkComm)
	secp256k1_scalar m_sk;
	secp256k1_scalar m_alpha;
	BeamCrypto_CompactPoint m_Commitment;

} BeamCrypto_RangeProof_Worker;

static void BeamCrypto_RangeProof_Calculate_Before_S(BeamCrypto_RangeProof_Worker* pWrk)
{
	const BeamCrypto_RangeProof* p = pWrk->m_pRangeProof;

	BeamCrypto_FlexPoint fp;
	BeamCrypto_CoinID_getSkComm(p->m_pKdf, &p->m_Cid, &pWrk->m_sk, &fp);

	BeamCrypto_FlexPoint_MakeCompact(&fp);
	assert(BeamCrypto_FlexPoint_Compact & fp.m_Flags);
	pWrk->m_Commitment = fp.m_Compact;

	// get seed
	BeamCrypto_Oracle oracle;
	secp256k1_sha256_initialize(&oracle.m_sha);
	secp256k1_sha256_write_Point(&oracle.m_sha, &fp);

	BeamCrypto_UintBig hv;
	secp256k1_sha256_finalize(&oracle.m_sha, hv.m_pVal);

	secp256k1_scalar k;
	BeamCrypto_Kdf_Derive_PKey(p->m_pKdf, &hv, &k);
	secp256k1_scalar_get_b32(hv.m_pVal, &k);

	secp256k1_sha256_initialize(&oracle.m_sha);
	secp256k1_sha256_write(&oracle.m_sha, hv.m_pVal, sizeof(hv.m_pVal));
	secp256k1_sha256_finalize(&oracle.m_sha, hv.m_pVal);

	// NonceGen
	static const char szSalt[] = "bulletproof";
	BeamCrypto_NonceGenerator_Init(&pWrk->m_NonceGen, szSalt, sizeof(szSalt), &hv);

	BeamCrypto_NonceGenerator_NextScalar(&pWrk->m_NonceGen, &pWrk->m_alpha); // alpha

	// embed params into alpha
	uint8_t* pPtr = hv.m_pVal + BeamCrypto_nBytes;
	WriteInNetworkOrder(&pPtr, p->m_Cid.m_Amount, sizeof(p->m_Cid.m_Amount));
	WriteInNetworkOrder(&pPtr, p->m_Cid.m_SubIdx, sizeof(p->m_Cid.m_SubIdx));
	WriteInNetworkOrder(&pPtr, p->m_Cid.m_Type, sizeof(p->m_Cid.m_Type));
	WriteInNetworkOrder(&pPtr, p->m_Cid.m_Idx, sizeof(p->m_Cid.m_Idx));
	WriteInNetworkOrder(&pPtr, p->m_Cid.m_AssetID, sizeof(p->m_Cid.m_AssetID));
	memset(hv.m_pVal, 0, pPtr - hv.m_pVal); // padding

	int overflow;
	secp256k1_scalar_set_b32(&k, hv.m_pVal, &overflow);
	assert(!overflow);

	secp256k1_scalar_add(&pWrk->m_alpha, &pWrk->m_alpha, &k);
}


#define nDims (sizeof(BeamCrypto_Amount) * 8)

static void BeamCrypto_RangeProof_Calculate_S(BeamCrypto_RangeProof_Worker* pWrk)
{
	// Data buffers needed for calculating Part1.S
	// Need to multi-exponentiate nDims * 2 == 128 elements.
	// Calculating everything in a single pass is faster, but requires more buffers (stack memory)
	// Each element size is sizeof(BeamCrypto_MultiMac_Scalar) + sizeof(BeamCrypto_MultiMac_WNaf),
	// which is either 34 or 68 bytes, depends on BeamCrypto_MultiMac_Directions (larger size is for faster algorithm)
	//
	// This requires 8.5K stack memory (or 4.25K if BeamCrypto_MultiMac_Directions == 1)
#define Calc_S_Naggle_Max (nDims * 2)

#define Calc_S_Naggle Calc_S_Naggle_Max // currently using max

	static_assert(Calc_S_Naggle <= Calc_S_Naggle_Max, "Naggle too large");

	BeamCrypto_MultiMac_Scalar pS[Calc_S_Naggle];
	BeamCrypto_MultiMac_WNaf pWnaf[Calc_S_Naggle];

	// Try to avoid local vars, save as much stack as possible

	secp256k1_scalar ro;
	BeamCrypto_NonceGenerator_NextScalar(&pWrk->m_NonceGen, &ro);

	BeamCrypto_MultiMac_Context mmCtx;
	mmCtx.m_pZDenom = 0;

	mmCtx.m_Secure = 1;
	mmCtx.m_pSecureK = &ro;
	mmCtx.m_pGenSecure = BeamCrypto_Context_get()->m_pGenGJ;

	mmCtx.m_Fast = 0;
	mmCtx.m_pGenFast = BeamCrypto_Context_get()->m_pGenFast;
	mmCtx.m_pS = pS;
	mmCtx.m_pWnaf = pWnaf;

	for (unsigned int iBit = 0; iBit < nDims * 2; iBit++, mmCtx.m_Fast++)
	{
		if (Calc_S_Naggle == mmCtx.m_Fast)
		{
			// flush
			mmCtx.m_pRes = pWrk->m_pGej + (iBit != Calc_S_Naggle); // 1st flush goes to pGej[0] directly
			BeamCrypto_MultiMac_Calculate(&mmCtx);

			if (iBit != Calc_S_Naggle)
				secp256k1_gej_add_var(pWrk->m_pGej, pWrk->m_pGej + 1, pWrk->m_pGej, 0);

			mmCtx.m_Secure = 0;
			mmCtx.m_Fast = 0;
			mmCtx.m_pGenFast += Calc_S_Naggle;
		}

		BeamCrypto_NonceGenerator_NextScalar(&pWrk->m_NonceGen, pS[mmCtx.m_Fast].m_pK);

		if (!(iBit % nDims) && pWrk->m_pRangeProof->m_pKExtra)
			// embed more info
			secp256k1_scalar_add(pS[mmCtx.m_Fast].m_pK, pS[mmCtx.m_Fast].m_pK, pWrk->m_pRangeProof->m_pKExtra + (iBit / nDims));
	}

	mmCtx.m_pRes = pWrk->m_pGej + 1;
	BeamCrypto_MultiMac_Calculate(&mmCtx);

	if (Calc_S_Naggle < Calc_S_Naggle_Max)
		secp256k1_gej_add_var(pWrk->m_pGej + 1, pWrk->m_pGej + 1, pWrk->m_pGej, 0);
}

static int BeamCrypto_RangeProof_Calculate_After_S(BeamCrypto_RangeProof_Worker* pWrk)
{
	BeamCrypto_RangeProof* p = pWrk->m_pRangeProof;
	BeamCrypto_Context* pCtx = BeamCrypto_Context_get();

	secp256k1_scalar pK[2];

	BeamCrypto_Oracle oracle;
	secp256k1_scalar pChallenge[2];
	BeamCrypto_UintBig hv;
	secp256k1_hmac_sha256_t hmac;

	BeamCrypto_FlexPoint pFp[2]; // 496 bytes
	pFp[1].m_Gej = pWrk->m_pGej[1];
	pFp[1].m_Flags = BeamCrypto_FlexPoint_Gej;

	// CalcA
	BeamCrypto_MultiMac_Context mmCtx;
	mmCtx.m_pZDenom = 0;
	mmCtx.m_Fast = 0;
	mmCtx.m_Secure = 1;
	mmCtx.m_pSecureK = &pWrk->m_alpha;
	mmCtx.m_pGenSecure = pCtx->m_pGenGJ;
	mmCtx.m_pRes = &pFp[0].m_Gej;

	BeamCrypto_MultiMac_Calculate(&mmCtx); // alpha*G
	pFp[0].m_Flags = BeamCrypto_FlexPoint_Gej;

	BeamCrypto_Amount v = p->m_Cid.m_Amount;

	for (uint32_t i = 0; i < nDims; i++)
	{
		if (1 & (v >> i))
			secp256k1_ge_from_storage(&pFp[0].m_Ge, pCtx->m_pGenFast[i].m_pPt);
		else
		{
			secp256k1_ge_from_storage(&pFp[0].m_Ge, pCtx->m_pGenFast[nDims + i].m_pPt);
			secp256k1_ge_neg(&pFp[0].m_Ge, &pFp[0].m_Ge);
		}

		secp256k1_gej_add_ge_var(&pFp[0].m_Gej, &pFp[0].m_Gej, &pFp[0].m_Ge, 0);
	}

	// normalize A,S at once, feed them to Oracle
	BeamCrypto_FlexPoint_MakeGe_Batch(pFp, _countof(pFp));

	BeamCrypto_Oracle_Init(&oracle);
	secp256k1_sha256_write_Num(&oracle.m_sha, 0); // incubation time, must be zero
	secp256k1_sha256_write_CompactPoint(&oracle.m_sha, &pWrk->m_Commitment); // starting from Fork1, earlier schem is not allowed

	for (unsigned int i = 0; i < 2; i++)
		secp256k1_sha256_write_Point(&oracle.m_sha, pFp + i);

	// get challenges. Use the challenges, sk, T1 and T2 to init the NonceGen for blinding the sk
	static const char szSalt[] = "bulletproof-sk";
	BeamCrypto_NonceGenerator_InitBegin(&pWrk->m_NonceGen, &hmac, szSalt, sizeof(szSalt));

	secp256k1_scalar_get_b32(hv.m_pVal, &pWrk->m_sk);
	secp256k1_hmac_sha256_write(&hmac, hv.m_pVal, sizeof(hv.m_pVal));

	for (unsigned int i = 0; i < 2; i++)
	{
		secp256k1_hmac_sha256_write(&hmac, p->m_pT[i].m_X.m_pVal, sizeof(p->m_pT[i].m_X.m_pVal));
		secp256k1_hmac_sha256_write(&hmac, &p->m_pT[i].m_Y, sizeof(p->m_pT[i].m_Y));

		BeamCrypto_Oracle_NextScalar(&oracle, pChallenge); // challenges y,z. The 'y' is not needed, will be overwritten by 'z'.
		secp256k1_scalar_get_b32(hv.m_pVal, pChallenge);
		secp256k1_hmac_sha256_write(&hmac, hv.m_pVal, sizeof(hv.m_pVal));
	}

	int ok = 1;

	BeamCrypto_NonceGenerator_InitEnd(&pWrk->m_NonceGen, &hmac);

	for (unsigned int i = 0; i < 2; i++)
	{
		BeamCrypto_NonceGenerator_NextScalar(&pWrk->m_NonceGen, pK + i); // tau1/2
		mmCtx.m_pSecureK = pK + i;
		mmCtx.m_pRes = pWrk->m_pGej;

		BeamCrypto_MultiMac_Calculate(&mmCtx); // pub nonces of T1/T2

		pFp[i].m_Compact = p->m_pT[i];
		pFp[i].m_Flags = BeamCrypto_FlexPoint_Compact;
		BeamCrypto_FlexPoint_MakeGe(pFp + i);
		if (!pFp[i].m_Flags)
		{
			ok = 0;
			break;
		}

		secp256k1_gej_add_ge_var(&pFp[i].m_Gej, mmCtx.m_pRes, &pFp[i].m_Ge, 0);
		pFp[i].m_Flags = BeamCrypto_FlexPoint_Gej;
	}

	SECURE_ERASE_OBJ(pWrk->m_NonceGen);

	if (ok)
	{
		// normalize & expose
		BeamCrypto_FlexPoint_MakeGe_Batch(pFp, _countof(pFp));

		for (unsigned int i = 0; i < 2; i++)
		{
			secp256k1_sha256_write_Point(&oracle.m_sha, pFp + i);
			assert(BeamCrypto_FlexPoint_Compact & pFp[i].m_Flags);
			p->m_pT[i] = pFp[i].m_Compact;
		}

		// last challenge
		BeamCrypto_Oracle_NextScalar(&oracle, pChallenge + 1);

		// m_TauX = tau2*x^2 + tau1*x + sk*z^2
		secp256k1_scalar_mul(pK, pK, pChallenge + 1); // tau1*x
		secp256k1_scalar_mul(pChallenge + 1, pChallenge + 1, pChallenge + 1); // x^2
		secp256k1_scalar_mul(pK + 1, pK + 1, pChallenge + 1); // tau2*x^2

		secp256k1_scalar_mul(pChallenge, pChallenge, pChallenge); // z^2

		secp256k1_scalar_mul(&p->m_TauX, &pWrk->m_sk, pChallenge); // sk*z^2
		secp256k1_scalar_add(&p->m_TauX, &p->m_TauX, pK);
		secp256k1_scalar_add(&p->m_TauX, &p->m_TauX, pK + 1);
	}

	SECURE_ERASE_OBJ(pWrk->m_sk);
	SECURE_ERASE_OBJ(pK); // tau1/2
	//SECURE_ERASE_OBJ(hv); - no need, last value is the challenge

	return ok;
}


int BeamCrypto_RangeProof_Calculate(BeamCrypto_RangeProof* p)
{
	BeamCrypto_RangeProof_Worker wrk;
	wrk.m_pRangeProof = p;

	BeamCrypto_RangeProof_Calculate_Before_S(&wrk);
	BeamCrypto_RangeProof_Calculate_S(&wrk);
	return BeamCrypto_RangeProof_Calculate_After_S(&wrk);
}

//////////////////////////////
// Signature
void BeamCrypto_Signature_GetChallengeEx(const BeamCrypto_CompactPoint* pNoncePub, const BeamCrypto_UintBig* pMsg, secp256k1_scalar* pE)
{
	BeamCrypto_Oracle oracle;
	BeamCrypto_Oracle_Init(&oracle);
	secp256k1_sha256_write_CompactPoint(&oracle.m_sha, pNoncePub);
	secp256k1_sha256_write(&oracle.m_sha, pMsg->m_pVal, sizeof(pMsg->m_pVal));

	BeamCrypto_Oracle_NextScalar(&oracle, pE);
}

void BeamCrypto_Signature_GetChallenge(const BeamCrypto_Signature* p, const BeamCrypto_UintBig* pMsg, secp256k1_scalar* pE)
{
	BeamCrypto_Signature_GetChallengeEx(&p->m_NoncePub, pMsg, pE);
}

void BeamCrypto_Signature_Sign(BeamCrypto_Signature* p, const BeamCrypto_UintBig* pMsg, const secp256k1_scalar* pSk)
{
	// get nonce
	secp256k1_hmac_sha256_t hmac;
	BeamCrypto_NonceGenerator ng;
	static const char szSalt[] = "beam-Schnorr";
	BeamCrypto_NonceGenerator_InitBegin(&ng, &hmac, szSalt, sizeof(szSalt));

	union
	{
		BeamCrypto_UintBig sk;
		secp256k1_scalar nonce;
	} u;

	static_assert(sizeof(u.nonce) >= sizeof(u.sk), ""); // means nonce completely overwrites the sk

	secp256k1_scalar_get_b32(u.sk.m_pVal, pSk);
	secp256k1_hmac_sha256_write(&hmac, u.sk.m_pVal, sizeof(u.sk.m_pVal));
	secp256k1_hmac_sha256_write(&hmac, pMsg->m_pVal, sizeof(pMsg->m_pVal));

	BeamCrypto_NonceGenerator_InitEnd(&ng, &hmac);
	BeamCrypto_NonceGenerator_NextScalar(&ng, &u.nonce);
	SECURE_ERASE_OBJ(ng);

	// expose the nonce
	BeamCrypto_FlexPoint fp;
	BeamCrypto_MulG(&fp, &u.nonce);
	BeamCrypto_FlexPoint_MakeCompact(&fp);
	p->m_NoncePub = fp.m_Compact;

	BeamCrypto_Signature_SignPartial(p, pMsg, pSk, &u.nonce);

	SECURE_ERASE_OBJ(u.nonce);
}

void BeamCrypto_Signature_SignPartialEx(BeamCrypto_UintBig* pRes, const secp256k1_scalar* pE, const secp256k1_scalar* pSk, const secp256k1_scalar* pNonce)
{
	secp256k1_scalar k;
	secp256k1_scalar_mul(&k, pE, pSk);
	secp256k1_scalar_add(&k, &k, pNonce);
	secp256k1_scalar_negate(&k, &k);

	secp256k1_scalar_get_b32(pRes->m_pVal, &k);
}

void BeamCrypto_Signature_SignPartial(BeamCrypto_Signature* p, const BeamCrypto_UintBig* pMsg, const secp256k1_scalar* pSk, const secp256k1_scalar* pNonce)
{
	secp256k1_scalar e;
	BeamCrypto_Signature_GetChallenge(p, pMsg, &e);
	BeamCrypto_Signature_SignPartialEx(&p->m_k, &e, pSk, pNonce);
}

int BeamCrypto_Signature_IsValid(const BeamCrypto_Signature* p, const BeamCrypto_UintBig* pMsg, BeamCrypto_FlexPoint* pPk)
{
	BeamCrypto_FlexPoint fpNonce;
	fpNonce.m_Compact = p->m_NoncePub;
	fpNonce.m_Flags = BeamCrypto_FlexPoint_Compact;
	
	BeamCrypto_FlexPoint_MakeGe(&fpNonce);
	if (!fpNonce.m_Flags)
		return 0;

	secp256k1_scalar k;
	int overflow; // for historical reasons we don't check for overflow, i.e. theoretically there can be an ambiguity, but it makes not much sense for the attacker
	secp256k1_scalar_set_b32(&k, p->m_k.m_pVal, &overflow);


	BeamCrypto_FlexPoint_MakeGej(pPk);
	if (!pPk->m_Flags)
		return 0; // bad Pubkey

	secp256k1_gej gej;
	secp256k1_fe zDenom;

	BeamCrypto_MultiMac_WNaf wnaf;
	BeamCrypto_MultiMac_Scalar s;
	BeamCrypto_MultiMac_Fast gen;

	BeamCrypto_MultiMac_Context ctx;
	ctx.m_pRes = &gej;
	ctx.m_Secure = 1;
	ctx.m_pGenSecure = BeamCrypto_Context_get()->m_pGenGJ;
	ctx.m_pSecureK = &k;

	if (secp256k1_gej_is_infinity(&pPk->m_Gej))
	{
		// unlikely, but allowed for historical reasons
		ctx.m_Fast = 0;
		ctx.m_pZDenom = 0;
	}
	else
	{
		ctx.m_pZDenom = &zDenom;
		ctx.m_Fast = 1;
		ctx.m_pGenFast = &gen;
		ctx.m_pS = &s;
		ctx.m_pWnaf = &wnaf;
		BeamCrypto_MultiMac_SetCustom_Nnz(&ctx, pPk);

		BeamCrypto_Signature_GetChallenge(p, pMsg, s.m_pK);
	}

	BeamCrypto_MultiMac_Calculate(&ctx);
	secp256k1_gej_add_ge_var(&gej, &gej, &fpNonce.m_Ge, 0);

	return secp256k1_gej_is_infinity(&gej);
}

//////////////////////////////
// TxKernel
void BeamCrypto_TxKernel_getID(const BeamCrypto_TxKernel* pKrn, BeamCrypto_UintBig* pMsg)
{
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);

	secp256k1_sha256_write_Num(&sha, pKrn->m_Fee);
	secp256k1_sha256_write_Num(&sha, pKrn->m_hMin);
	secp256k1_sha256_write_Num(&sha, pKrn->m_hMax);

	secp256k1_sha256_write_CompactPoint(&sha, &pKrn->m_Commitment);
	secp256k1_sha256_write_Num(&sha, 0); // former m_AssetEmission

	uint8_t nFlags = 0; // extended flags, irrelevent for HW wallet
	secp256k1_sha256_write(&sha, &nFlags, sizeof(nFlags));

	nFlags = 1; // no more nested kernels
	secp256k1_sha256_write(&sha, &nFlags, sizeof(nFlags));

	secp256k1_sha256_finalize(&sha, pMsg->m_pVal);
}

int BeamCrypto_TxKernel_IsValid(const BeamCrypto_TxKernel* pKrn)
{
	BeamCrypto_UintBig msg;
	BeamCrypto_TxKernel_getID(pKrn, &msg);

	BeamCrypto_FlexPoint fp;
	fp.m_Compact = pKrn->m_Commitment;
	fp.m_Flags = BeamCrypto_FlexPoint_Compact;

	return BeamCrypto_Signature_IsValid(&pKrn->m_Signature, &msg, &fp);
}

//////////////////////////////
// KeyKeeper - pub Kdf export
static void Kdf2Pub(const BeamCrypto_Kdf* pKdf, BeamCrypto_KdfPub* pRes)
{
	BeamCrypto_Context* pCtx = BeamCrypto_Context_get();

	pRes->m_Secret = pKdf->m_Secret;

	BeamCrypto_FlexPoint fp;

	BeamCrypto_MulPoint(&fp, pCtx->m_pGenGJ, &pKdf->m_kCoFactor);
	BeamCrypto_FlexPoint_MakeCompact(&fp);
	pRes->m_CoFactorG = fp.m_Compact;

	BeamCrypto_MulPoint(&fp, pCtx->m_pGenGJ + 1, &pKdf->m_kCoFactor);
	BeamCrypto_FlexPoint_MakeCompact(&fp);
	pRes->m_CoFactorJ = fp.m_Compact;
}

void BeamCrypto_KeyKeeper_GetPKdf(const BeamCrypto_KeyKeeper* p, BeamCrypto_KdfPub* pRes, const uint32_t* pChild)
{
	if (pChild)
	{
		BeamCrypto_Kdf kdfChild;
		BeamCrypto_Kdf_getChild(&kdfChild, *pChild, &p->m_MasterKey);
		Kdf2Pub(&kdfChild, pRes);
	}
	else
		Kdf2Pub(&p->m_MasterKey, pRes);
}

//////////////////////////////
// KeyKeeper - transaction common. Aggregation
typedef struct
{
	BeamCrypto_Amount m_Beams;
	BeamCrypto_Amount m_Assets;

} TxAggr0;

typedef struct
{
	TxAggr0 m_Ins;
	TxAggr0 m_Outs;

	BeamCrypto_Amount m_TotalFee;
	BeamCrypto_AssetID m_AssetID;
	secp256k1_scalar m_sk;

} TxAggr;


static int TxAggregate_AddAmount(BeamCrypto_Amount val, BeamCrypto_AssetID aid, TxAggr0* pRes, TxAggr* pCommon)
{
	BeamCrypto_Amount* pVal;
	if (aid)
	{
		if (pCommon->m_AssetID)
		{
			if (pCommon->m_AssetID != aid)
				return 0; // multiple assets are not allowed
		}
		else
			pCommon->m_AssetID = aid;

		pVal = &pRes->m_Assets;
	}
	else
		pVal = &pRes->m_Beams;

	(*pVal) += val;

	if (val > (*pVal))
		return 0; // overflow

	return 1;
}

static int TxAggregate0(const BeamCrypto_KeyKeeper* p, const BeamCrypto_CoinID* pCid, unsigned int nCount, TxAggr0* pRes, TxAggr* pCommon, int isOuts)
{
	for (unsigned int i = 0; i < nCount; i++)
	{
		uint8_t nScheme;
		uint32_t nSubkey;
		BeamCrypto_CoinID_getSchemeAndSubkey(pCid + i, &nScheme, &nSubkey);

		if (nSubkey && isOuts)
			return 0; // HW wallet should not send funds to child subkeys (potentially belonging to miners)

		switch (nScheme)
		{
		case BeamCrypto_CoinID_Scheme_V0:
		case BeamCrypto_CoinID_Scheme_BB21:
			// weak schemes
			if (isOuts)
				return 0; // no reason to create weak outputs

			if (!p->m_AllowWeakInputs)
				return 0;
		}

		if (!TxAggregate_AddAmount(pCid[i].m_Amount, pCid[i].m_AssetID, pRes, pCommon))
			return 0;

		secp256k1_scalar sk;
		BeamCrypto_CoinID_getSk(&p->m_MasterKey, pCid + i, &sk);

		secp256k1_scalar_add(&pCommon->m_sk, &pCommon->m_sk, &sk);
		SECURE_ERASE_OBJ(sk);
	}

	return 1;
}

static int TxAggregateShIns(const BeamCrypto_KeyKeeper* p, const BeamCrypto_ShieldedInput* pIns, unsigned int nCount, TxAggr0* pRes, TxAggr* pCommon)
{
	for (unsigned int i = 0; i < nCount; i++)
	{
		if (!TxAggregate_AddAmount(pIns[i].m_TxoID.m_Amount, pIns[i].m_TxoID.m_AssetID, pRes, pCommon))
			return 0;

		pCommon->m_TotalFee += pIns[i].m_Fee;
		if (pCommon->m_TotalFee < pIns[i].m_Fee)
			return 0; // overflow

		secp256k1_scalar sk;
		BeamCrypto_ShieldedInput_getSk(&p->m_MasterKey, pIns + i, &sk);

		secp256k1_scalar_add(&pCommon->m_sk, &pCommon->m_sk, &sk);
		SECURE_ERASE_OBJ(sk);
	}

	return 1;
}

static int TxAggregate(const BeamCrypto_KeyKeeper* p, const BeamCrypto_TxCommon* pTx, TxAggr* pRes)
{
	memset(pRes, 0, sizeof(*pRes));
	pRes->m_TotalFee = pTx->m_Krn.m_Fee;

	if (!TxAggregate0(p, pTx->m_pIns, pTx->m_Ins, &pRes->m_Ins, pRes, 0))
		return 0;

	if (!TxAggregateShIns(p, pTx->m_pInsShielded, pTx->m_InsShielded, &pRes->m_Ins, pRes))
		return 0;

	secp256k1_scalar_negate(&pRes->m_sk, &pRes->m_sk);

	return TxAggregate0(p, pTx->m_pOuts, pTx->m_Outs, &pRes->m_Outs, pRes, 1);
}

static void TxAggrToOffset(TxAggr* pAggr, const secp256k1_scalar* pKrn, BeamCrypto_TxCommon* pTx)
{
	secp256k1_scalar_add(&pAggr->m_sk, &pAggr->m_sk, pKrn);
	secp256k1_scalar_negate(&pAggr->m_sk, &pAggr->m_sk);
	secp256k1_scalar_get_b32(pTx->m_kOffset.m_pVal, &pAggr->m_sk);
}

static void TxImportSubtract(secp256k1_scalar* pK, const BeamCrypto_UintBig* pPrev)
{
	secp256k1_scalar kPeer;
	int overflow;
	secp256k1_scalar_set_b32(&kPeer, pPrev->m_pVal, &overflow);
	secp256k1_scalar_negate(&kPeer, &kPeer);
	secp256k1_scalar_add(pK, pK, &kPeer);
}

static int TxAggregate_SendOrSplit(const BeamCrypto_KeyKeeper* p, const BeamCrypto_TxCommon* pTx, TxAggr* pRes)
{
	if (!TxAggregate(p, pTx, pRes))
		return 0;

	if (pRes->m_Ins.m_Beams < pRes->m_Outs.m_Beams)
		return 0; // not sending
	pRes->m_Ins.m_Beams -= pRes->m_Outs.m_Beams;

	if (pRes->m_Ins.m_Assets != pRes->m_Outs.m_Assets)
	{
		if (pRes->m_Ins.m_Assets < pRes->m_Outs.m_Assets)
			return 0; // not sending

		if (pRes->m_Ins.m_Beams != pRes->m_TotalFee)
			return 0; // balance mismatch, the lost amount must go entirely to fee

		pRes->m_Ins.m_Assets -= pRes->m_Outs.m_Assets;
	}
	else
	{
		if (pRes->m_Ins.m_Beams < pRes->m_TotalFee)
			return 0; // not sending

		pRes->m_Ins.m_Assets = pRes->m_Ins.m_Beams - pRes->m_TotalFee;
		pRes->m_AssetID = 0;
	}

	return 1;
}

//////////////////////////////
// KeyKeeper - user permission required
static int BeamCrypto_KeyKeeper_ConfirmSpend(BeamCrypto_Amount val, BeamCrypto_AssetID aid, const BeamCrypto_UintBig* pPeerID, const BeamCrypto_TxKernel* pKrn, const BeamCrypto_UintBig* pKrnID)
{
	// pPeerID is NULL, if it's a Split tx.
	// pKrnID may be NULL, if this is a 'preliminary' confirmation (SendTx 1st invocation)

	return BeamCrypto_KeyKeeper_Status_Ok; // TODO
}

//////////////////////////////
// KeyKeeper - Kernel modification
static int KernelUpdateKeys(BeamCrypto_TxKernel* pKrn, const secp256k1_scalar* pSk, const secp256k1_scalar* pNonce, int nAdd)
{
	BeamCrypto_FlexPoint pFp[2];

	BeamCrypto_MulG(pFp, pSk);
	BeamCrypto_MulG(pFp + 1, pNonce);

	if (nAdd)
	{
		BeamCrypto_FlexPoint fp;
		fp.m_Compact = pKrn->m_Commitment;
		fp.m_Flags = BeamCrypto_FlexPoint_Compact;

		BeamCrypto_FlexPoint_MakeGe(&fp);
		if (!fp.m_Flags)
			return 0;

		secp256k1_gej_add_ge_var(&pFp[0].m_Gej, &pFp[0].m_Gej, &fp.m_Ge, 0);

		fp.m_Compact = pKrn->m_Signature.m_NoncePub;
		fp.m_Flags = BeamCrypto_FlexPoint_Compact;

		BeamCrypto_FlexPoint_MakeGe(&fp);
		if (!fp.m_Flags)
			return 0;

		secp256k1_gej_add_ge_var(&pFp[1].m_Gej, &pFp[1].m_Gej, &fp.m_Ge, 0);
	}

	BeamCrypto_FlexPoint_MakeGe_Batch(pFp, _countof(pFp));

	BeamCrypto_FlexPoint_MakeCompact(pFp);
	pKrn->m_Commitment = pFp[0].m_Compact;

	BeamCrypto_FlexPoint_MakeCompact(pFp + 1);
	pKrn->m_Signature.m_NoncePub = pFp[1].m_Compact;

	return 1;
}

//////////////////////////////
// KeyKeeper - SplitTx
int BeamCrypto_KeyKeeper_SignTx_Split(const BeamCrypto_KeyKeeper* p, BeamCrypto_TxCommon* pTx)
{
	TxAggr txAggr;
	if (!TxAggregate_SendOrSplit(p, pTx, &txAggr))
		return BeamCrypto_KeyKeeper_Status_Unspecified;
	if (txAggr.m_Ins.m_Assets)
		return BeamCrypto_KeyKeeper_Status_Unspecified; // not split

	// hash all visible params
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);
	secp256k1_sha256_write_Num(&sha, pTx->m_Krn.m_hMin);
	secp256k1_sha256_write_Num(&sha, pTx->m_Krn.m_hMax);
	secp256k1_sha256_write_Num(&sha, pTx->m_Krn.m_Fee);

	BeamCrypto_UintBig hv;
	secp256k1_scalar_get_b32(hv.m_pVal, &txAggr.m_sk);
	secp256k1_sha256_write(&sha, hv.m_pVal, sizeof(hv.m_pVal));
	secp256k1_sha256_finalize(&sha, hv.m_pVal);

	// derive keys
	static const char szSalt[] = "hw-wlt-split";
	BeamCrypto_NonceGenerator ng;
	BeamCrypto_NonceGenerator_Init(&ng, szSalt, sizeof(szSalt), &hv);

	secp256k1_scalar kKrn, kNonce;
	BeamCrypto_NonceGenerator_NextScalar(&ng, &kKrn);
	BeamCrypto_NonceGenerator_NextScalar(&ng, &kNonce);
	SECURE_ERASE_OBJ(ng);

	KernelUpdateKeys(&pTx->m_Krn, &kKrn, &kNonce, 0);

	BeamCrypto_TxKernel_getID(&pTx->m_Krn, &hv);

	int res = BeamCrypto_KeyKeeper_ConfirmSpend(0, 0, 0, &pTx->m_Krn, &hv);
	if (BeamCrypto_KeyKeeper_Status_Ok != res)
		return res;

	BeamCrypto_Signature_SignPartial(&pTx->m_Krn.m_Signature, &hv, &kKrn, &kNonce);

	TxAggrToOffset(&txAggr, &kKrn, pTx);

	return BeamCrypto_KeyKeeper_Status_Ok;
}

//////////////////////////////
// KeyKeeper - Receive + Send common stuff
static void GetPaymentConfirmationMsg(BeamCrypto_UintBig* pRes, const BeamCrypto_UintBig* pSender, const BeamCrypto_UintBig* pKernelID, BeamCrypto_Amount amount, BeamCrypto_AssetID nAssetID)
{
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);

	HASH_WRITE_STR(sha, "PaymentConfirmation");
	secp256k1_sha256_write(&sha, pKernelID->m_pVal, sizeof(pKernelID->m_pVal));
	secp256k1_sha256_write(&sha, pSender->m_pVal, sizeof(pSender->m_pVal));
	secp256k1_sha256_write_Num(&sha, amount);

	if (nAssetID)
	{
		HASH_WRITE_STR(sha, "asset");
		secp256k1_sha256_write_Num(&sha, nAssetID);
	}

	secp256k1_sha256_finalize(&sha, pRes->m_pVal);
}

static void GetWalletIDKey(const BeamCrypto_KeyKeeper* p, BeamCrypto_WalletIdentity nKey, secp256k1_scalar* pKey, BeamCrypto_UintBig* pID)
{
	// derive key
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);
	HASH_WRITE_STR(sha, "kid");

	const uint32_t nType = FOURCC_FROM_STR(tRid);

	secp256k1_sha256_write_Num(&sha, nKey);
	secp256k1_sha256_write_Num(&sha, nType);
	secp256k1_sha256_write_Num(&sha, 0);
	secp256k1_sha256_finalize(&sha, pID->m_pVal);

	BeamCrypto_Kdf_Derive_SKey(&p->m_MasterKey, pID, pKey);
	BeamCrypto_Sk2Pk(pID, pKey);
}

//////////////////////////////
// KeyKeeper - ReceiveTx
int BeamCrypto_KeyKeeper_SignTx_Receive(const BeamCrypto_KeyKeeper* p, BeamCrypto_TxCommon* pTx, BeamCrypto_TxMutualInfo* pMut)
{
	TxAggr txAggr;
	if (!TxAggregate(p, pTx, &txAggr))
		return BeamCrypto_KeyKeeper_Status_Unspecified;

	if (txAggr.m_Ins.m_Beams != txAggr.m_Outs.m_Beams)
	{
		if (txAggr.m_Ins.m_Beams > txAggr.m_Outs.m_Beams)
			return BeamCrypto_KeyKeeper_Status_Unspecified; // not receiving

		if (txAggr.m_Ins.m_Assets != txAggr.m_Outs.m_Assets)
			return BeamCrypto_KeyKeeper_Status_Unspecified; // mixed

		txAggr.m_AssetID = 0;
		txAggr.m_Outs.m_Assets = txAggr.m_Outs.m_Beams - txAggr.m_Ins.m_Beams;
	}
	else
	{
		if (txAggr.m_Ins.m_Assets >= txAggr.m_Outs.m_Assets)
			return BeamCrypto_KeyKeeper_Status_Unspecified; // not receiving

		assert(txAggr.m_AssetID);
		txAggr.m_Outs.m_Assets -= txAggr.m_Ins.m_Assets;
	}

	// Hash *ALL* the parameters, make the context unique
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);

	BeamCrypto_UintBig hv;
	BeamCrypto_TxKernel_getID(&pTx->m_Krn, &hv); // not a final ID yet

	secp256k1_sha256_write(&sha, hv.m_pVal, sizeof(hv.m_pVal));
	secp256k1_sha256_write_CompactPoint(&sha, &pTx->m_Krn.m_Signature.m_NoncePub);

	uint8_t nFlag = 0; // not nonconventional
	secp256k1_sha256_write(&sha, &nFlag, sizeof(nFlag));
	secp256k1_sha256_write(&sha, pMut->m_Peer.m_pVal, sizeof(pMut->m_Peer.m_pVal));
	secp256k1_sha256_write_Num(&sha, pMut->m_MyIDKey);

	secp256k1_scalar_get_b32(hv.m_pVal, &txAggr.m_sk);
	secp256k1_sha256_write(&sha, hv.m_pVal, sizeof(hv.m_pVal));

	secp256k1_sha256_write_Num(&sha, txAggr.m_Outs.m_Assets); // the value being-received
	secp256k1_sha256_write_Num(&sha, txAggr.m_AssetID);

	secp256k1_sha256_finalize(&sha, hv.m_pVal);

	// derive keys
	static const char szSalt[] = "hw-wlt-rcv";
	BeamCrypto_NonceGenerator ng;
	BeamCrypto_NonceGenerator_Init(&ng, szSalt, sizeof(szSalt), &hv);

	secp256k1_scalar kKrn, kNonce;
	BeamCrypto_NonceGenerator_NextScalar(&ng, &kKrn);
	BeamCrypto_NonceGenerator_NextScalar(&ng, &kNonce);
	SECURE_ERASE_OBJ(ng);

	if (!KernelUpdateKeys(&pTx->m_Krn, &kKrn, &kNonce, 1))
		return BeamCrypto_KeyKeeper_Status_Unspecified;

	BeamCrypto_TxKernel_getID(&pTx->m_Krn, &hv); // final ID
	BeamCrypto_Signature_SignPartial(&pTx->m_Krn.m_Signature, &hv, &kKrn, &kNonce);

	TxAggrToOffset(&txAggr, &kKrn, pTx);

	if (pMut->m_MyIDKey)
	{
		// sign
		BeamCrypto_UintBig hvID;
		GetWalletIDKey(p, pMut->m_MyIDKey, &kKrn, &hvID);
		GetPaymentConfirmationMsg(&hvID, &pMut->m_Peer, &hv, txAggr.m_Outs.m_Assets, txAggr.m_AssetID);
		BeamCrypto_Signature_Sign(&pMut->m_PaymentProofSignature, &hvID, &kKrn);
	}

	return BeamCrypto_KeyKeeper_Status_Ok;
}

//////////////////////////////
// KeyKeeper - SendTx
int BeamCrypto_KeyKeeper_SignTx_Send(const BeamCrypto_KeyKeeper* p, BeamCrypto_TxCommon* pTx, BeamCrypto_TxMutualInfo* pMut, BeamCrypto_TxSenderParams* pSnd)
{
	TxAggr txAggr;
	if (!TxAggregate_SendOrSplit(p, pTx, &txAggr))
		return BeamCrypto_KeyKeeper_Status_Unspecified;
	if (!txAggr.m_Ins.m_Assets)
		return BeamCrypto_KeyKeeper_Status_Unspecified; // not sending (no net transferred value)

	if (IsUintBigZero(&pMut->m_Peer))
		return BeamCrypto_KeyKeeper_Status_UserAbort; // conventional transfers must always be signed

	secp256k1_scalar kKrn, kNonce;
	BeamCrypto_UintBig hvMyID, hv;
	GetWalletIDKey(p, pMut->m_MyIDKey, &kNonce, &hvMyID);

	if (pSnd->m_iSlot >= BeamCrypto_KeyKeeper_getNumSlots())
		return BeamCrypto_KeyKeeper_Status_Unspecified;

	BeamCrypto_KeyKeeper_ReadSlot(pSnd->m_iSlot, &hv);
	BeamCrypto_Kdf_Derive_SKey(&p->m_MasterKey, &hv, &kNonce);

	// during negotiation kernel height and commitment are adjusted. We should only commit to the Fee
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);
	secp256k1_sha256_write_Num(&sha, pTx->m_Krn.m_Fee);
	secp256k1_sha256_write(&sha, pMut->m_Peer.m_pVal, sizeof(pMut->m_Peer.m_pVal));
	secp256k1_sha256_write(&sha, hvMyID.m_pVal, sizeof(hvMyID.m_pVal));

	uint8_t nFlag = 0; // not nonconventional
	secp256k1_sha256_write(&sha, &nFlag, sizeof(nFlag));

	secp256k1_scalar_get_b32(hv.m_pVal, &txAggr.m_sk);
	secp256k1_sha256_write(&sha, hv.m_pVal, sizeof(hv.m_pVal));
	secp256k1_sha256_write_Num(&sha, txAggr.m_Ins.m_Assets);
	secp256k1_sha256_write_Num(&sha, txAggr.m_AssetID);

	secp256k1_scalar_get_b32(hv.m_pVal, &kNonce);
	secp256k1_sha256_write(&sha, hv.m_pVal, sizeof(hv.m_pVal));
	secp256k1_sha256_finalize(&sha, hv.m_pVal);

	static const char szSalt[] = "hw-wlt-snd";
	BeamCrypto_NonceGenerator ng;
	BeamCrypto_NonceGenerator_Init(&ng, szSalt, sizeof(szSalt), &hv);
	BeamCrypto_NonceGenerator_NextScalar(&ng, &kKrn);
	SECURE_ERASE_OBJ(ng);

	// derive tx token
	secp256k1_sha256_initialize(&sha);
	HASH_WRITE_STR(sha, "tx.token");

	secp256k1_scalar_get_b32(hv.m_pVal, &kKrn);
	secp256k1_sha256_write(&sha, hv.m_pVal, sizeof(hv.m_pVal));
	secp256k1_sha256_finalize(&sha, hv.m_pVal);

	if (IsUintBigZero(&hv))
		hv.m_pVal[_countof(hv.m_pVal) - 1] = 1;

	if (IsUintBigZero(&pSnd->m_UserAgreement))
	{
		int res = BeamCrypto_KeyKeeper_ConfirmSpend(txAggr.m_Ins.m_Assets, txAggr.m_AssetID, &pMut->m_Peer, &pTx->m_Krn, 0);
		if (BeamCrypto_KeyKeeper_Status_Ok != res)
			return res;

		pSnd->m_UserAgreement = hv;

		KernelUpdateKeys(&pTx->m_Krn, &kKrn, &kNonce, 0);

		return BeamCrypto_KeyKeeper_Status_Ok;
	}

	if (memcmp(pSnd->m_UserAgreement.m_pVal, hv.m_pVal, sizeof(hv.m_pVal)))
		return BeamCrypto_KeyKeeper_Status_Unspecified; // incorrect user agreement token

	BeamCrypto_TxKernel_getID(&pTx->m_Krn, &hv);

	// verify payment confirmation signature
	GetPaymentConfirmationMsg(&hvMyID, &hvMyID, &hv, txAggr.m_Ins.m_Assets, txAggr.m_AssetID);

	BeamCrypto_FlexPoint fp;
	fp.m_Compact.m_X = pMut->m_Peer;
	fp.m_Compact.m_Y = 0;
	fp.m_Flags = BeamCrypto_FlexPoint_Compact;

	if (!BeamCrypto_Signature_IsValid(&pMut->m_PaymentProofSignature, &hvMyID, &fp))
		return BeamCrypto_KeyKeeper_Status_Unspecified;

	// 2nd user confirmation request. Now the kernel is complete, its ID is calculated
	int res = BeamCrypto_KeyKeeper_ConfirmSpend(txAggr.m_Ins.m_Assets, txAggr.m_AssetID, &pMut->m_Peer, &pTx->m_Krn, &hvMyID);
	if (BeamCrypto_KeyKeeper_Status_Ok != res)
		return res;

	// Regenerate the slot (BEFORE signing), and sign
	BeamCrypto_KeyKeeper_RegenerateSlot(pSnd->m_iSlot);

	TxImportSubtract(&kNonce, &pTx->m_Krn.m_Signature.m_k);
	BeamCrypto_TxKernel_getID(&pTx->m_Krn, &hv); // final ID
	BeamCrypto_Signature_SignPartial(&pTx->m_Krn.m_Signature, &hv, &kKrn, &kNonce);

	TxImportSubtract(&kKrn, &pTx->m_kOffset);
	TxAggrToOffset(&txAggr, &kKrn, pTx);

	return BeamCrypto_KeyKeeper_Status_Ok;
}

//////////////////////////////
// Voucher
static void ShieldedHashTxt(secp256k1_sha256_t* pSha)
{
	secp256k1_sha256_initialize(pSha);
	HASH_WRITE_STR(*pSha, "Output.Shielded.");
}

typedef struct
{
	BeamCrypto_Kdf m_Gen;
	BeamCrypto_Kdf m_Ser;

} ShieldedViewer;

static void ShieldedViewerInit(ShieldedViewer* pRes, uint32_t iViewer, const BeamCrypto_KeyKeeper* p)
{
	// Shielded viewer
	BeamCrypto_UintBig hv;
	secp256k1_sha256_t sha;
	secp256k1_scalar sk;

	ShieldedHashTxt(&sha);
	HASH_WRITE_STR(sha, "Own.Gen");
	secp256k1_sha256_write_Num(&sha, iViewer);
	secp256k1_sha256_finalize(&sha, hv.m_pVal);

	BeamCrypto_Kdf_Derive_PKey(&p->m_MasterKey, &hv, &sk);
	secp256k1_scalar_get_b32(hv.m_pVal, &sk);

	BeamCrypto_Kdf_Init(&pRes->m_Gen, &hv);

	ShieldedHashTxt(&sha);
	HASH_WRITE_STR(sha, "Own.Ser");
	secp256k1_sha256_write_Num(&sha, iViewer);
	secp256k1_sha256_finalize(&sha, hv.m_pVal);

	BeamCrypto_Kdf_Derive_PKey(&p->m_MasterKey, &hv, &sk);
	secp256k1_scalar_get_b32(hv.m_pVal, &sk);

	BeamCrypto_Kdf_Derive_PKey(&p->m_MasterKey, &hv, &sk);
	secp256k1_scalar_get_b32(hv.m_pVal, &sk);

	BeamCrypto_Kdf_Init(&pRes->m_Ser, &hv);
	secp256k1_scalar_mul(&pRes->m_Ser.m_kCoFactor, &pRes->m_Ser.m_kCoFactor, &sk);
}

static void BeamCrypto_MulGJ(BeamCrypto_FlexPoint* pFlex, const secp256k1_scalar* pK)
{
	BeamCrypto_MultiMac_Context ctx;
	ctx.m_pRes = &pFlex->m_Gej;
	ctx.m_pZDenom = 0;
	ctx.m_Fast = 0;
	ctx.m_Secure = 2;
	ctx.m_pGenSecure = BeamCrypto_Context_get()->m_pGenGJ;
	ctx.m_pSecureK = pK;

	BeamCrypto_MultiMac_Calculate(&ctx);
	pFlex->m_Flags = BeamCrypto_FlexPoint_Gej;
}

static void BeamCrypto_Ticket_Hash(BeamCrypto_UintBig* pRes, const BeamCrypto_ShieldedVoucher* pVoucher)
{
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);
	HASH_WRITE_STR(sha, "Out-S");
	secp256k1_sha256_write_CompactPoint(&sha, &pVoucher->m_SerialPub);
	secp256k1_sha256_finalize(&sha, pRes->m_pVal);
}

static void BeamCrypto_Voucher_Hash(BeamCrypto_UintBig* pRes, const BeamCrypto_ShieldedVoucher* pVoucher)
{
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);
	HASH_WRITE_STR(sha, "voucher.1");
	secp256k1_sha256_write_CompactPoint(&sha, &pVoucher->m_SerialPub);
	secp256k1_sha256_write_CompactPoint(&sha, &pVoucher->m_NoncePub);
	secp256k1_sha256_write(&sha, pVoucher->m_SharedSecret.m_pVal, sizeof(pVoucher->m_SharedSecret.m_pVal));
	secp256k1_sha256_finalize(&sha, pRes->m_pVal);
}

static void BeamCrypto_CreateVoucherInternal(BeamCrypto_ShieldedVoucher* pRes, const BeamCrypto_UintBig* pNonce, const ShieldedViewer* pViewer)
{
	secp256k1_scalar pK[2], pN[2], sk;
	BeamCrypto_UintBig hv;
	BeamCrypto_Oracle oracle;

	// nonce -> kG
	ShieldedHashTxt(&oracle.m_sha);
	HASH_WRITE_STR(oracle.m_sha, "kG");
	secp256k1_sha256_write(&oracle.m_sha, pNonce->m_pVal, sizeof(pNonce->m_pVal));
	secp256k1_sha256_finalize(&oracle.m_sha, hv.m_pVal);
	BeamCrypto_Kdf_Derive_PKey(&pViewer->m_Gen, &hv, pK);

	// kG -> serial preimage
	ShieldedHashTxt(&oracle.m_sha);
	HASH_WRITE_STR(oracle.m_sha, "kG-k");
	secp256k1_scalar_get_b32(hv.m_pVal, pK);
	secp256k1_sha256_write(&oracle.m_sha, hv.m_pVal, sizeof(hv.m_pVal));
	secp256k1_sha256_finalize(&oracle.m_sha, hv.m_pVal);
	BeamCrypto_Kdf_Derive_SKey(&pViewer->m_Gen, &hv, &sk);

	ShieldedHashTxt(&oracle.m_sha);
	HASH_WRITE_STR(oracle.m_sha, "k-pI");
	secp256k1_scalar_get_b32(hv.m_pVal, &sk);
	secp256k1_sha256_write(&oracle.m_sha, hv.m_pVal, sizeof(hv.m_pVal));
	secp256k1_sha256_finalize(&oracle.m_sha, hv.m_pVal); // SerialPreimage

	BeamCrypto_Kdf_Derive_SKey(&pViewer->m_Ser, &hv, &sk); // spend sk

	BeamCrypto_FlexPoint pt;
	BeamCrypto_MulG(&pt, &sk); // spend pk

	BeamCrypto_Oracle_Init(&oracle);
	HASH_WRITE_STR(oracle.m_sha, "L.Spend");
	secp256k1_sha256_write_Point(&oracle.m_sha, &pt);
	BeamCrypto_Oracle_NextScalar(&oracle, pK + 1); // serial

	BeamCrypto_MulGJ(&pt, pK);
	BeamCrypto_FlexPoint_MakeCompact(&pt);
	pRes->m_SerialPub = pt.m_Compact; // kG*G + serial*J

	// DH
	ShieldedHashTxt(&oracle.m_sha);
	HASH_WRITE_STR(oracle.m_sha, "DH");
	secp256k1_sha256_write_CompactPoint(&oracle.m_sha, &pRes->m_SerialPub);
	secp256k1_sha256_finalize(&oracle.m_sha, hv.m_pVal);
	BeamCrypto_Kdf_Derive_SKey(&pViewer->m_Gen, &hv, &sk); // DH multiplier

	secp256k1_scalar_mul(pN, pK, &sk);
	secp256k1_scalar_mul(pN + 1, pK + 1, &sk);
	BeamCrypto_MulGJ(&pt, pN); // shared point

	ShieldedHashTxt(&oracle.m_sha);
	HASH_WRITE_STR(oracle.m_sha, "sp-sec");
	secp256k1_sha256_write_Point(&oracle.m_sha, &pt);
	secp256k1_sha256_finalize(&oracle.m_sha, pRes->m_SharedSecret.m_pVal); // Shared secret

	// nonces
	ShieldedHashTxt(&oracle.m_sha);
	HASH_WRITE_STR(oracle.m_sha, "nG");
	secp256k1_sha256_write(&oracle.m_sha, pRes->m_SharedSecret.m_pVal, sizeof(pRes->m_SharedSecret.m_pVal));
	secp256k1_sha256_finalize(&oracle.m_sha, hv.m_pVal);
	BeamCrypto_Kdf_Derive_PKey(&pViewer->m_Gen, &hv, pN);

	ShieldedHashTxt(&oracle.m_sha);
	HASH_WRITE_STR(oracle.m_sha, "nJ");
	secp256k1_sha256_write(&oracle.m_sha, pRes->m_SharedSecret.m_pVal, sizeof(pRes->m_SharedSecret.m_pVal));
	secp256k1_sha256_finalize(&oracle.m_sha, hv.m_pVal);
	BeamCrypto_Kdf_Derive_PKey(&pViewer->m_Gen, &hv, pN + 1);

	BeamCrypto_MulGJ(&pt, pN);
	BeamCrypto_FlexPoint_MakeCompact(&pt);
	pRes->m_NoncePub = pt.m_Compact; // nG*G + nJ*J

	// sign it
	BeamCrypto_Ticket_Hash(&hv, pRes);
	BeamCrypto_Signature_GetChallengeEx(&pRes->m_NoncePub, &hv, &sk);
	BeamCrypto_Signature_SignPartialEx(pRes->m_pK, &sk, pK, pN);
	BeamCrypto_Signature_SignPartialEx(pRes->m_pK + 1, &sk, pK + 1, pN + 1);
}

int BeamCrypto_KeyKeeper_CreateVouchers(const BeamCrypto_KeyKeeper* p, BeamCrypto_ShieldedVoucher* pRes, uint32_t n, BeamCrypto_WalletIdentity nMyIDKey, BeamCrypto_UintBig* pNonce0)
{
	if (!n)
		return BeamCrypto_KeyKeeper_Status_Ok;

	ShieldedViewer viewer;
	ShieldedViewerInit(&viewer, 0, p);

	// key to sign the voucher(s)
	BeamCrypto_UintBig hv;
	secp256k1_scalar skSign;
	GetWalletIDKey(p, nMyIDKey, &skSign, &hv);

	for (uint32_t i = 0; ; pRes++)
	{
		BeamCrypto_CreateVoucherInternal(pRes, pNonce0, &viewer);

		BeamCrypto_Voucher_Hash(&hv, pRes);
		BeamCrypto_Signature_Sign(&pRes->m_Signature, &hv, &skSign);

		if (++i == n)
			break;

		// regenerate nonce
		BeamCrypto_Oracle oracle;
		secp256k1_sha256_initialize(&oracle.m_sha);
		HASH_WRITE_STR(oracle.m_sha, "sh.v.n");
		secp256k1_sha256_write(&oracle.m_sha, pNonce0->m_pVal, sizeof(pNonce0->m_pVal));
		secp256k1_sha256_finalize(&oracle.m_sha, pNonce0->m_pVal);
	}

	return BeamCrypto_KeyKeeper_Status_Ok;
}

