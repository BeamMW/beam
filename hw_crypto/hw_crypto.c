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

#define SECURE_ERASE_OBJ(x) memset(&x, 0, sizeof(x))

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

BeamCrypto_Context g_Context;

BeamCrypto_Context* BeamCrypto_Context_get()
{
	return &g_Context;
}

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

static void BeamCrypto_ToCommonDenominator(unsigned int nCount, secp256k1_gej* pGej, secp256k1_fe* pFe, secp256k1_fe* pZDenom, int nNormalize)
{
	assert(nCount);

	pFe[0] = pGej[0].z;

	for (unsigned int i = 1; i < nCount; i++)
		secp256k1_fe_mul(pFe + i, pFe + i - 1, &pGej[i].z);

	if (nNormalize)
		secp256k1_fe_inv(pZDenom, pFe + nCount - 1); // the only expensive call
	else
		secp256k1_fe_set_int(pZDenom, 1);

	for (unsigned int i = nCount; i--; )
	{
		if (i)
			secp256k1_fe_mul(pFe + i, pFe + i - 1, pZDenom);
		else
			pFe[i] = *pZDenom;

		secp256k1_gej_rescale_XY(pGej + i, pFe + i);

		secp256k1_fe_mul(pZDenom, pZDenom, &pGej[i].z);
	}
}

static void secp256k1_ge_set_gej_normalized(secp256k1_ge* pGe, const secp256k1_gej* pGej)
{
	pGe->infinity = pGej->infinity;
	pGe->x = pGej->x;
	pGe->y = pGej->y;
}

static void BeamCrypto_MultiMac_SetCustom_Nnz(BeamCrypto_MultiMac_Context* p, secp256k1_gej* pOdds)
{
	assert(p->m_Fast == 1);
	assert(p->m_pZDenom);

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

static void BeamCrypto_MultiMac_SetCustom_ge_Nnz(BeamCrypto_MultiMac_Context* p, const secp256k1_ge* pGe)
{
	assert(!secp256k1_ge_is_infinity(pGe));

	secp256k1_gej pOdds[BeamCrypto_MultiMac_Fast_nCount];
	secp256k1_gej_set_ge(pOdds, pGe);

	BeamCrypto_MultiMac_SetCustom_Nnz(p, pOdds);
}

static void BeamCrypto_MultiMac_SetCustom_gej_Nnz(BeamCrypto_MultiMac_Context* p, const secp256k1_gej* pGej)
{
	assert(!secp256k1_gej_is_infinity(pGej));

	secp256k1_gej pOdds[BeamCrypto_MultiMac_Fast_nCount];
	pOdds[0] = *pGej;

	BeamCrypto_MultiMac_SetCustom_Nnz(p, pOdds);
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

void BeamCrypto_NonceGenerator_NextScalar(BeamCrypto_NonceGenerator* p, secp256k1_scalar* pS)
{
	while (1)
	{
		BeamCrypto_NonceGenerator_NextOkm(p);

		int overflow;
		secp256k1_scalar_set_b32(pS, p->m_Okm.m_pVal, &overflow);
		if (!overflow)
			break;
	}
}

//////////////////////////////
// Point
int BeamCrypto_Point_ImportNnz(const BeamCrypto_Point* pPt, secp256k1_ge* pGe)
{
	if (pPt->m_Y > 1)
		return 0; // should always be well-formed

	secp256k1_fe nx;
	if (!secp256k1_fe_set_b32(&nx, pPt->m_X.m_pVal))
		return 0;

	if (!secp256k1_ge_set_xo_var(pGe, &nx, pPt->m_Y))
		return 0;

	return 1;
}

void BeamCrypto_Point_Export_Raw(BeamCrypto_Point* pPt, const secp256k1_fe* pX, const secp256k1_fe* pY)
{
	secp256k1_fe_get_b32(pPt->m_X.m_pVal, pX);
	pPt->m_Y = (secp256k1_fe_is_odd(pY) != 0);
}

void BeamCrypto_Point_Export(BeamCrypto_Point* pPt, const secp256k1_ge* pGe)
{
	if (secp256k1_ge_is_infinity(pGe))
		memset(pPt, 0, sizeof(*pPt));
	else
		BeamCrypto_Point_Export_Raw(pPt, &pGe->x, &pGe->y);
}

void BeamCrypto_MulPoint(BeamCrypto_Point* pRes, const BeamCrypto_MultiMac_Secure* pGen, const secp256k1_scalar* pK)
{
	secp256k1_ge ge;
	secp256k1_gej gej;
	BeamCrypto_MultiMac_Context ctx;
	ctx.m_pRes = &gej;
	ctx.m_pZDenom = 0;
	ctx.m_Fast = 0;
	ctx.m_Secure = 1;
	ctx.m_pGenSecure = pGen;
	ctx.m_pSecureK = pK;

	BeamCrypto_MultiMac_Calculate(&ctx);

	secp256k1_ge_set_gej_var(&ge, &gej);
	BeamCrypto_Point_Export(pRes, &ge);
}

void BeamCrypto_MulG(BeamCrypto_Point* pRes, const secp256k1_scalar* pK)
{
	BeamCrypto_MulPoint(pRes, &BeamCrypto_Context_get()->m_GenG, pK);
}

void BeamCrypto_Sk2Pk(BeamCrypto_UintBig* pRes, secp256k1_scalar* pK)
{
	BeamCrypto_Point pt;
	BeamCrypto_MulG(&pt, pK);

	*pRes = pt.m_X;

	if (pt.m_Y)
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

		int overflow;
		secp256k1_scalar_set_b32(pS, hash.m_pVal, &overflow);
		if (!overflow)
			break;
	}
}

void BeamCrypto_Oracle_NextPoint(BeamCrypto_Oracle* p, secp256k1_ge* pGe)
{
	BeamCrypto_Point pt;
	pt.m_Y = 0;

	while (1)
	{
		BeamCrypto_Oracle_NextHash(p, &pt.m_X);
		if (BeamCrypto_Point_ImportNnz(&pt, pGe))
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

#define HASH_WRITE_STR(hash, str) secp256k1_sha256_write(&hash, str, sizeof(str))

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

void secp256k1_sha256_write_Point(secp256k1_sha256_t* pSha, const BeamCrypto_Point* pPt)
{
	secp256k1_sha256_write(pSha, pPt->m_X.m_pVal, sizeof(pPt->m_X.m_pVal));
	secp256k1_sha256_write(pSha, &pPt->m_Y, sizeof(pPt->m_Y));
}

void secp256k1_sha256_write_Ge(secp256k1_sha256_t* pSha, const secp256k1_ge* pGe)
{
	BeamCrypto_Point pt;
	BeamCrypto_Point_Export(&pt, pGe);
	secp256k1_sha256_write_Point(pSha, &pt);
}

void secp256k1_sha256_write_Gej(secp256k1_sha256_t* pSha, secp256k1_gej* pGej)
{
	secp256k1_ge ge;
	secp256k1_ge_set_gej_var(&ge, pGej); // expensive
	secp256k1_sha256_write_Ge(pSha, &ge);
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

void BeamCrypto_CoinID_getSkComm(const BeamCrypto_Kdf* pKdf, const BeamCrypto_CoinID* pCid, secp256k1_scalar* pK, BeamCrypto_Point* pComm)
{
	secp256k1_gej pGej[2];

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
			secp256k1_ge ge;
			secp256k1_scalar k1;
		} o;

		struct
		{
			BeamCrypto_MultiMac_Scalar s;
			BeamCrypto_MultiMac_WNaf wnaf;
			BeamCrypto_MultiMac_Fast genAsset;
			secp256k1_fe zDenom;
		} mm;

		secp256k1_fe pFe[_countof(pGej) + 1];

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
	mmCtx.m_pRes = pGej;
	mmCtx.m_Secure = 1;
	mmCtx.m_pSecureK = pK;
	mmCtx.m_pGenSecure = &pCtx->m_GenG;
	mmCtx.m_Fast = 1;
	mmCtx.m_pS = &u.mm.s;
	mmCtx.m_pWnaf = &u.mm.wnaf;

	if (pCid->m_AssetID)
	{
		// derive asset gen
		BeamCrypto_Oracle_Init(&u.o.oracle);

		HASH_WRITE_STR(u.o.oracle.m_sha, "B.Asset.Gen.V1");
		secp256k1_sha256_write_Num(&u.o.oracle.m_sha, pCid->m_AssetID);

		secp256k1_ge geAsset;
		BeamCrypto_Oracle_NextPoint(&u.o.oracle, &geAsset);

		mmCtx.m_pGenFast = &u.mm.genAsset;
		mmCtx.m_pZDenom = &u.mm.zDenom;

		BeamCrypto_MultiMac_SetCustom_ge_Nnz(&mmCtx, &geAsset);

	}
	else
	{
		mmCtx.m_pGenFast = pCtx->m_pGenFast + BeamCrypto_MultiMac_Fast_Idx_H;
		mmCtx.m_pZDenom = 0;
	}

	secp256k1_scalar_set_u64(u.mm.s.m_pK, pCid->m_Amount);

	BeamCrypto_MultiMac_Calculate(&mmCtx);

	// sk * J
	mmCtx.m_pRes = pGej + 1;
	mmCtx.m_pGenSecure = &pCtx->m_GenJ;
	mmCtx.m_pZDenom = 0;
	mmCtx.m_Fast = 0;

	BeamCrypto_MultiMac_Calculate(&mmCtx);

	// adjust sk
	BeamCrypto_ToCommonDenominator(_countof(pGej), pGej, u.pFe, u.pFe + _countof(pGej), 1);

	BeamCrypto_Oracle_Init(&u.o.oracle);

	for (unsigned int i = 0; i < _countof(pGej); i++)
	{
		secp256k1_ge_set_gej_normalized(&u.o.ge, pGej + i);
		secp256k1_sha256_write_Ge(&u.o.oracle.m_sha, &u.o.ge);
	}

	BeamCrypto_Oracle_NextScalar(&u.o.oracle, &u.o.k1);

	secp256k1_scalar_add(pK, pK, &u.o.k1);

	if (pComm)
	{
		mmCtx.m_pGenSecure = &pCtx->m_GenG; // not really secure here, just no good reason to have additional non-secure J-gen
		mmCtx.m_pSecureK = &u.o.k1;

		BeamCrypto_MultiMac_Calculate(&mmCtx);

		secp256k1_ge_set_gej_normalized(&u.o.ge, pGej); // pGej[0] is already normalized
		secp256k1_gej_add_ge_var(pGej + 1, pGej + 1, &u.o.ge, 0);

		secp256k1_ge_set_gej(&u.o.ge, pGej + 1);
		BeamCrypto_Point_Export(pComm, &u.o.ge);
	}
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

int BeamCrypto_RangeProof_Calculate(BeamCrypto_RangeProof* p)
{
	secp256k1_scalar sk, pK[2];
	BeamCrypto_Point comm;
	BeamCrypto_NonceGenerator ng;
	secp256k1_gej pGej[2];

#define nDims (sizeof(BeamCrypto_Amount) * 8)

	union
	{
		struct
		{
			// Data buffers needed for calculating Part1.S
			// Need to multi-exponentiate nDims * 2 == 128 elements.
			// Calculating everything in a single pass is faster, but requires more buffers (stack memory)
			// Each element size is sizeof(BeamCrypto_MultiMac_Scalar) + sizeof(BeamCrypto_MultiMac_WNaf),
			// which is either 34 or 68 bytes, depends on BeamCrypto_MultiMac_Directions (larger size is for faster algorithm)
			//
			// This requires 8.5K stack memory (or 4.25K if BeamCrypto_MultiMac_Directions == 1)
#define Calc_S_Naggle_Max (nDims * 2)

#define Calc_S_Naggle Calc_S_Naggle_Max

			BeamCrypto_MultiMac_Scalar pS[Calc_S_Naggle];
			BeamCrypto_MultiMac_WNaf pWnaf[Calc_S_Naggle];
		} s;

		struct
		{
			// all that we don't need during calculating Part1.S
			BeamCrypto_Oracle oracle;
			secp256k1_scalar pChallenge[2];
			BeamCrypto_UintBig hv;
			secp256k1_hmac_sha256_t hmac;
			secp256k1_ge ge;
			secp256k1_fe pFe[3];
		} x;
	} u;

	static_assert(Calc_S_Naggle <= Calc_S_Naggle_Max, "Naggle too large");

	BeamCrypto_CoinID_getSkComm(p->m_pKdf, &p->m_Cid, &sk, &comm);

	// get seed
	secp256k1_sha256_initialize(&u.x.oracle.m_sha);
	secp256k1_sha256_write_Point(&u.x.oracle.m_sha, &comm);
	secp256k1_sha256_finalize(&u.x.oracle.m_sha, u.x.hv.m_pVal);

	BeamCrypto_Kdf_Derive_PKey(p->m_pKdf, &u.x.hv, pK);
	secp256k1_scalar_get_b32(u.x.hv.m_pVal, pK);

	secp256k1_sha256_initialize(&u.x.oracle.m_sha);
	secp256k1_sha256_write(&u.x.oracle.m_sha, u.x.hv.m_pVal, sizeof(u.x.hv.m_pVal));
	secp256k1_sha256_finalize(&u.x.oracle.m_sha, u.x.hv.m_pVal);

	// NonceGen
	static const char szSalt[] = "bulletproof";
	BeamCrypto_NonceGenerator_Init(&ng, szSalt, sizeof(szSalt), &u.x.hv);

	BeamCrypto_NonceGenerator_NextScalar(&ng, pK); // alpha

	// embed params into alpha
	uint8_t* pPtr = u.x.hv.m_pVal + BeamCrypto_nBytes;
	WriteInNetworkOrder(&pPtr, p->m_Cid.m_Amount, sizeof(p->m_Cid.m_Amount));
	WriteInNetworkOrder(&pPtr, p->m_Cid.m_SubIdx, sizeof(p->m_Cid.m_SubIdx));
	WriteInNetworkOrder(&pPtr, p->m_Cid.m_Type, sizeof(p->m_Cid.m_Type));
	WriteInNetworkOrder(&pPtr, p->m_Cid.m_Idx, sizeof(p->m_Cid.m_Idx));
	WriteInNetworkOrder(&pPtr, p->m_Cid.m_AssetID, sizeof(p->m_Cid.m_AssetID));
	memset(u.x.hv.m_pVal, 0, pPtr - u.x.hv.m_pVal); // padding

	int overflow;
	secp256k1_scalar_set_b32(pK + 1, u.x.hv.m_pVal, &overflow);
	assert(!overflow);

	secp256k1_scalar_add(pK, pK, pK + 1);

	BeamCrypto_Context* pCtx = BeamCrypto_Context_get();

	// CalcS
	BeamCrypto_NonceGenerator_NextScalar(&ng, pK + 1);

	BeamCrypto_MultiMac_Context mmCtx;
	mmCtx.m_pZDenom = 0;

	mmCtx.m_Secure = 1;
	mmCtx.m_pSecureK = pK + 1;
	mmCtx.m_pGenSecure = &pCtx->m_GenG;

	mmCtx.m_Fast = 0;
	mmCtx.m_pGenFast = pCtx->m_pGenFast;
	mmCtx.m_pS = u.s.pS;
	mmCtx.m_pWnaf = u.s.pWnaf;

	for (unsigned int iBit = 0; iBit < nDims * 2; iBit++, mmCtx.m_Fast++)
	{
		if (Calc_S_Naggle == mmCtx.m_Fast)
		{
			// flush
			mmCtx.m_pRes = pGej + (iBit != Calc_S_Naggle); // 1st flush goes to pGej[0] directly
			BeamCrypto_MultiMac_Calculate(&mmCtx);

			if (iBit != Calc_S_Naggle)
				secp256k1_gej_add_var(pGej, pGej + 1, pGej, 0);

			mmCtx.m_Secure = 0;
			mmCtx.m_Fast = 0;
			mmCtx.m_pGenFast += Calc_S_Naggle;
		}

		BeamCrypto_NonceGenerator_NextScalar(&ng, u.s.pS[mmCtx.m_Fast].m_pK);

		if (!(iBit % nDims) && p->m_pKExtra)
			// embed more info
			secp256k1_scalar_add(u.s.pS[mmCtx.m_Fast].m_pK, u.s.pS[mmCtx.m_Fast].m_pK, p->m_pKExtra + (iBit / nDims));
	}

	mmCtx.m_pRes = pGej + 1;
	BeamCrypto_MultiMac_Calculate(&mmCtx);

	if (Calc_S_Naggle < Calc_S_Naggle_Max)
		secp256k1_gej_add_var(pGej + 1, pGej + 1, pGej, 0);

	// CalcA
	mmCtx.m_pRes = pGej;
	mmCtx.m_Fast = 0;
	mmCtx.m_Secure = 1;
	mmCtx.m_pSecureK = pK;
	BeamCrypto_MultiMac_Calculate(&mmCtx); // alpha*G

	BeamCrypto_Amount v = p->m_Cid.m_Amount;

	for (uint32_t i = 0; i < nDims; i++)
	{
		if (1 & (v >> i))
			secp256k1_ge_from_storage(&u.x.ge, pCtx->m_pGenFast[i].m_pPt);
		else
		{
			secp256k1_ge_from_storage(&u.x.ge, pCtx->m_pGenFast[nDims + i].m_pPt);
			secp256k1_ge_neg(&u.x.ge, &u.x.ge);
		}

		secp256k1_gej_add_ge_var(pGej, pGej, &u.x.ge, 0);
	}

	// oracle
	BeamCrypto_Oracle_Init(&u.x.oracle);
	secp256k1_sha256_write_Num(&u.x.oracle.m_sha, 0); // incubation time, must be zero
	secp256k1_sha256_write_Point(&u.x.oracle.m_sha, &comm); // starting from Fork1, earlier schem is not allowed

	// normalize A,S at once, feed them to Oracle
	BeamCrypto_ToCommonDenominator(2, pGej, u.x.pFe, u.x.pFe + 2, 1);

	for (unsigned int i = 0; i < 2; i++)
	{
		BeamCrypto_Point_Export_Raw(&comm, &pGej[i].x, &pGej[i].y);
		secp256k1_sha256_write_Point(&u.x.oracle.m_sha, &comm);
	}

	// get challenges. Use the challenges, sk, T1 and T2 to init the NonceGen for blinding the sk
	BeamCrypto_NonceGenerator_InitBegin(&ng, &u.x.hmac, szSalt, sizeof(szSalt)); // same salt, nevermind

	secp256k1_scalar_get_b32(u.x.hv.m_pVal, &sk);
	secp256k1_hmac_sha256_write(&u.x.hmac, u.x.hv.m_pVal, sizeof(u.x.hv.m_pVal));

	for (unsigned int i = 0; i < 2; i++)
	{
		secp256k1_hmac_sha256_write(&u.x.hmac, p->m_pT[i].m_X.m_pVal, sizeof(p->m_pT[i].m_X.m_pVal));
		secp256k1_hmac_sha256_write(&u.x.hmac, &p->m_pT[i].m_Y, sizeof(p->m_pT[i].m_Y));

		BeamCrypto_Oracle_NextScalar(&u.x.oracle, u.x.pChallenge); // challenges y,z. The 'y' is not needed, will be overwritten by 'z'.
		secp256k1_scalar_get_b32(u.x.hv.m_pVal, u.x.pChallenge);
		secp256k1_hmac_sha256_write(&u.x.hmac, u.x.hv.m_pVal, sizeof(u.x.hv.m_pVal));
	}

	int ok = 1;

	BeamCrypto_NonceGenerator_InitEnd(&ng, &u.x.hmac);

	for (unsigned int i = 0; i < 2; i++)
	{
		BeamCrypto_NonceGenerator_NextScalar(&ng, pK + i); // tau1/2
		mmCtx.m_pSecureK = pK + i;
		mmCtx.m_pRes = pGej + i;

		BeamCrypto_MultiMac_Calculate(&mmCtx); // pub nonces of T1/T2

		if (!BeamCrypto_Point_ImportNnz(p->m_pT + i, &u.x.ge))
		{
			ok = 0;
			break;
		}

		secp256k1_gej_add_ge_var(mmCtx.m_pRes, mmCtx.m_pRes, &u.x.ge, 0);
	}

	SECURE_ERASE_OBJ(ng);

	if (ok)
	{
		// normalize & expose
		BeamCrypto_ToCommonDenominator(2, pGej, u.x.pFe, u.x.pFe + 2, 1);

		for (unsigned int i = 0; i < 2; i++)
		{
			BeamCrypto_Point_Export_Raw(p->m_pT + i, &pGej[i].x, &pGej[i].y);
			secp256k1_sha256_write_Point(&u.x.oracle.m_sha, p->m_pT + i);
		}

		// last challenge
		BeamCrypto_Oracle_NextScalar(&u.x.oracle, u.x.pChallenge + 1);

		// m_TauX = tau2*x^2 + tau1*x + sk*z^2
		secp256k1_scalar_mul(pK, pK, u.x.pChallenge + 1); // tau1*x
		secp256k1_scalar_mul(u.x.pChallenge + 1, u.x.pChallenge + 1, u.x.pChallenge + 1); // x^2
		secp256k1_scalar_mul(pK + 1, pK + 1, u.x.pChallenge + 1); // tau2*x^2

		secp256k1_scalar_mul(u.x.pChallenge, u.x.pChallenge, u.x.pChallenge); // z^2

		secp256k1_scalar_mul(&p->m_TauX, &sk, u.x.pChallenge); // sk*z^2
		secp256k1_scalar_add(&p->m_TauX, &p->m_TauX, pK);
		secp256k1_scalar_add(&p->m_TauX, &p->m_TauX, pK + 1);
	}

	SECURE_ERASE_OBJ(sk);
	SECURE_ERASE_OBJ(pK); // tau1/2
	//SECURE_ERASE_OBJ(hv); - no need, last value is the challenge

	return ok;
}

//////////////////////////////
// Signature
void BeamCrypto_Signature_GetChallenge(const BeamCrypto_Signature* p, const BeamCrypto_UintBig* pMsg, secp256k1_scalar* pE)
{
	BeamCrypto_Oracle oracle;
	BeamCrypto_Oracle_Init(&oracle);
	secp256k1_sha256_write_Point(&oracle.m_sha, &p->m_NoncePub);
	secp256k1_sha256_write(&oracle.m_sha, pMsg->m_pVal, sizeof(pMsg->m_pVal));

	BeamCrypto_Oracle_NextScalar(&oracle, pE);
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
	BeamCrypto_MulG(&p->m_NoncePub, &u.nonce);

	secp256k1_scalar e;
	BeamCrypto_Signature_GetChallenge(p, pMsg, &e);

	secp256k1_scalar_mul(&e, &e, pSk);
	secp256k1_scalar_add(&e, &e, &u.nonce);
	secp256k1_scalar_negate(&e, &e);

	secp256k1_scalar_get_b32(p->m_k.m_pVal, &e);
	SECURE_ERASE_OBJ(u.nonce);
}

int BeamCrypto_Signature_IsValid_Gej(const BeamCrypto_Signature* p, const BeamCrypto_UintBig* pMsg, const secp256k1_gej* pPk)
{
	secp256k1_ge ge;
	if (!BeamCrypto_Point_ImportNnz(&p->m_NoncePub, &ge))
		return 0;

	secp256k1_scalar k;
	int overflow; // for historical reasons we don't check for overflow, i.e. theoretically there can be an ambiguity, but it makes not much sense for the attacker
	secp256k1_scalar_set_b32(&k, p->m_k.m_pVal, &overflow);


	secp256k1_gej gej;
	secp256k1_fe zDenom;

	BeamCrypto_MultiMac_WNaf wnaf;
	BeamCrypto_MultiMac_Scalar s;
	BeamCrypto_MultiMac_Fast gen;

	BeamCrypto_MultiMac_Context ctx;
	ctx.m_pRes = &gej;
	ctx.m_Secure = 1;
	ctx.m_pGenSecure = &BeamCrypto_Context_get()->m_GenG;
	ctx.m_pSecureK = &k;

	if (secp256k1_gej_is_infinity(pPk))
	{
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
		BeamCrypto_MultiMac_SetCustom_gej_Nnz(&ctx, pPk);

		BeamCrypto_Signature_GetChallenge(p, pMsg, s.m_pK);
	}

	BeamCrypto_MultiMac_Calculate(&ctx);
	secp256k1_gej_add_ge_var(&gej, &gej, &ge, 0);

	return secp256k1_gej_is_infinity(&gej);
}

int BeamCrypto_Signature_IsValid_Pt(const BeamCrypto_Signature* p, const BeamCrypto_UintBig* pMsg, const BeamCrypto_Point* pPk)
{
	secp256k1_ge ge;
	if (!BeamCrypto_Point_ImportNnz(pPk, &ge))
		return 0;

	secp256k1_gej gej;
	secp256k1_gej_set_ge(&gej, &ge);

	return BeamCrypto_Signature_IsValid_Gej(p, pMsg, &gej);
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

	secp256k1_sha256_write_Point(&sha, &pKrn->m_Commitment);
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

	return BeamCrypto_Signature_IsValid_Pt(&pKrn->m_Signature, &msg, &pKrn->m_Commitment);
}

//////////////////////////////
// KeyKeeper - pub Kdf export
static void Kdf2Pub(const BeamCrypto_Kdf* pKdf, BeamCrypto_KdfPub* pRes)
{
	BeamCrypto_Context* pCtx = BeamCrypto_Context_get();

	pRes->m_Secret = pKdf->m_Secret;

	BeamCrypto_MulPoint(&pRes->m_CoFactorG, &pCtx->m_GenG, &pKdf->m_kCoFactor);
	BeamCrypto_MulPoint(&pRes->m_CoFactorJ, &pCtx->m_GenJ, &pKdf->m_kCoFactor);
}

void BeamCrypto_KeyKeeper_GetKdfPub(const BeamCrypto_KeyKeeper* p, BeamCrypto_KdfPub* pRes, uint32_t* pChild)
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
