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

#define __stack_hungry__


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

#define SECURE_ERASE_OBJ(x) SecureEraseMem(&x, sizeof(x))

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

inline static secp256k1_scalar_uint BitWalker_xor(const BitWalker* p, secp256k1_scalar* pK)
{
	return (pK->d[p->m_Word] ^= p->m_Msk) & p->m_Msk;
}

#define c_WNaf_Invalid 0x80
static_assert((c_MultiMac_Fast_nCount * 2) < c_WNaf_Invalid, "");

inline static void WNaf_Cursor_SetInvalid(MultiMac_WNaf* p)
{
	p->m_iBit = 0xff;
	p->m_iElement = c_WNaf_Invalid;
}


static int WNaf_Cursor_Init(MultiMac_WNaf* p, secp256k1_scalar* pK)
{
	// Choose the optimal strategy
	// Pass from lower bits up, look for 1.
	WNaf_Cursor_SetInvalid(p);

	int carry = 0;
	unsigned int nWndLen = 0;
	BitWalker bw, bw0;
	bw.m_Word = 0;
	bw.m_Msk = 1;

	for (unsigned int iBit = 0; iBit < c_ECC_nBits; BitWalker_MoveUp(&bw), iBit++)
	{
		if (carry)
		{
			assert(!nWndLen);

			if (!BitWalker_xor(&bw, pK))
				continue;

			carry = 0;
		}
		else
		{
			secp256k1_scalar_uint val = BitWalker_get(&bw, pK);

			if (nWndLen)
			{
				assert(nWndLen <= c_MultiMac_Fast_nBits);

				if (val)
					p->m_iElement |= (1 << (nWndLen - 1));

				if (++nWndLen > c_MultiMac_Fast_nBits)
				{
					// word end. Make sure there's a bit set at this position. Use the bit at word beginning as a negation indicator.
					if (val)
					{
						carry = 1;
						BitWalker_xor(&bw0, pK);
					}
					else
						BitWalker_xor(&bw, pK);

					nWndLen = 0;
				}

				continue;
			}

			if (!val)
				continue;
		}

		// new window
		nWndLen = 1;
		p->m_iBit = iBit;
		p->m_iElement = 0;

		bw0 = bw;
	}

	return carry;
}

static void WNaf_Cursor_MoveNext(MultiMac_WNaf* p, const secp256k1_scalar* pK)
{
	if (p->m_iBit <= c_MultiMac_Fast_nBits)
		return;

	BitWalker bw;
	BitWalker_SetPos(&bw, --p->m_iBit);

	// find next nnz bit
	for (; ; BitWalker_MoveDown(&bw), --p->m_iBit)
	{
		if (BitWalker_get(&bw, pK))
			break;

		if (p->m_iBit <= c_MultiMac_Fast_nBits)
		{
			WNaf_Cursor_SetInvalid(p);
			return;
		}
	}

	p->m_iBit -= c_MultiMac_Fast_nBits;
	p->m_iElement = 0;

	for (unsigned int i = 0; i < (c_MultiMac_Fast_nBits - 1); i++)
	{
		p->m_iElement <<= 1;

		BitWalker_MoveDown(&bw);
		if (BitWalker_get(&bw, pK))
			p->m_iElement |= 1;
	}

	assert(p->m_iElement < c_MultiMac_Fast_nCount);

	// last indicator bit
	BitWalker_MoveDown(&bw);
	if (!BitWalker_get(&bw, pK))
		// must negate instead of addition
		p->m_iElement += c_MultiMac_Fast_nCount;
}

void mem_cmov(unsigned int* pDst, const unsigned int* pSrc, int flag, unsigned int nWords)
{
	const unsigned int mask0 = flag + ~((unsigned int) 0);
	const unsigned int mask1 = ~mask0;

	for (unsigned int n = 0; n < nWords; n++)
		pDst[n] = (pDst[n] & mask0) | (pSrc[n] & mask1);
}

__stack_hungry__
static void MultiMac_Calculate_PrePhase(const MultiMac_Context* p)
{
	secp256k1_gej_set_infinity(p->m_pRes);

	for (unsigned int i = 0; i < p->m_Fast; i++)
	{
		MultiMac_WNaf* pWnaf = p->m_pWnaf + i;
		secp256k1_scalar* pS = p->m_pFastK + i;

		int carry = WNaf_Cursor_Init(pWnaf, pS);
		if (carry)
		{
			secp256k1_ge ge;
			secp256k1_ge_from_storage(&ge, p->m_pGenFast[i].m_pPt);
			secp256k1_gej_add_ge_var(p->m_pRes, p->m_pRes, &ge, 0);
		}
	}
}

__stack_hungry__
static void MultiMac_Calculate_Secure_Read(secp256k1_ge* pGe, const MultiMac_Secure* pGen, unsigned int iElement)
{
	secp256k1_ge_storage ges;

	for (unsigned int j = 0; j < c_MultiMac_Secure_nCount; j++)
	{
		static_assert(sizeof(ges) == sizeof(pGen->m_pPt[j]), "");
		static_assert(!(sizeof(ges) % sizeof(unsigned int)), "");

		mem_cmov(
			(unsigned int*) &ges,
			(unsigned int*)(pGen->m_pPt + j),
			iElement == j,
			sizeof(ges) / sizeof(unsigned int));
	}

	secp256k1_ge_from_storage(pGe, &ges); // inline is ok here
	SECURE_ERASE_OBJ(ges);
}

__stack_hungry__
static void MultiMac_Calculate_SecureBit(const MultiMac_Context* p, unsigned int iBit)
{
	secp256k1_ge ge;

	static_assert(!(secp256k1_scalar_WordBits % c_MultiMac_Secure_nBits), "");

	unsigned int iWord = iBit / secp256k1_scalar_WordBits;
	unsigned int nShift = iBit % secp256k1_scalar_WordBits;
	const secp256k1_scalar_uint nMsk = ((1U << c_MultiMac_Secure_nBits) - 1);

	for (unsigned int i = 0; i < p->m_Secure; i++)
	{
		unsigned int iElement = (p->m_pSecureK[i].d[iWord] >> nShift) & nMsk;
		const MultiMac_Secure* pGen = p->m_pGenSecure + i;

		MultiMac_Calculate_Secure_Read(&ge, pGen, iElement);

		if (p->m_pZDenom)
			secp256k1_gej_add_zinv_var(p->m_pRes, p->m_pRes, &ge, p->m_pZDenom);
		else
			secp256k1_gej_add_ge_var(p->m_pRes, p->m_pRes, &ge, 0);
	}
}

__stack_hungry__
static void MultiMac_Calculate_FastBit(const MultiMac_Context* p, unsigned int iBit)
{
	for (unsigned int i = 0; i < p->m_Fast; i++)
	{
		MultiMac_WNaf* pWnaf = p->m_pWnaf + i;

		if (((uint8_t)iBit) != pWnaf->m_iBit)
			continue;

		unsigned int iElem = pWnaf->m_iElement;

		if (c_WNaf_Invalid == iElem)
			continue;

		int bNegate = (iElem >= c_MultiMac_Fast_nCount);
		if (bNegate)
		{
			iElem = (c_MultiMac_Fast_nCount * 2 - 1) - iElem;
			assert(iElem < c_MultiMac_Fast_nCount);
		}

		secp256k1_ge ge;
		secp256k1_ge_from_storage(&ge, p->m_pGenFast[i].m_pPt + iElem);

		if (bNegate)
			secp256k1_ge_neg(&ge, &ge);

		secp256k1_gej_add_ge_var(p->m_pRes, p->m_pRes, &ge, 0);

		WNaf_Cursor_MoveNext(pWnaf, p->m_pFastK + i);
	}
}

__stack_hungry__
static void MultiMac_Calculate_PostPhase(const MultiMac_Context* p)
{
	if (p->m_pZDenom)
		// fix denominator
		secp256k1_fe_mul(&p->m_pRes->z, &p->m_pRes->z, p->m_pZDenom);

	for (unsigned int i = 0; i < p->m_Secure; i++)
	{
		secp256k1_ge ge;
		secp256k1_ge_from_storage(&ge, p->m_pGenSecure[i].m_pPt + c_MultiMac_Secure_nCount);
		secp256k1_gej_add_ge_var(p->m_pRes, p->m_pRes, &ge, 0);
	}

}

__stack_hungry__
void MultiMac_Calculate(const MultiMac_Context* p)
{
	MultiMac_Calculate_PrePhase(p);

	for (unsigned int iBit = c_ECC_nBits; iBit--; )
	{
		secp256k1_gej_double_var(p->m_pRes, p->m_pRes, 0); // would be fast if zero, no need to check explicitly

		if (!(iBit % c_MultiMac_Secure_nBits) && p->m_Secure)
			MultiMac_Calculate_SecureBit(p, iBit);


		MultiMac_Calculate_FastBit(p, iBit);
	}

	MultiMac_Calculate_PostPhase(p);
}

//////////////////////////////
// Batch normalization
__stack_hungry__
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


static void ToCommonDenominator(unsigned int nCount, secp256k1_gej* pGej, secp256k1_fe* pFe, secp256k1_fe* pZDenom, int nNormalize)
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

__stack_hungry__
static void MultiMac_SetCustom_Nnz(MultiMac_Context* p, FlexPoint* pFlex)
{
	assert(p->m_Fast == 1);
	assert(p->m_pZDenom);

	FlexPoint_MakeGej(pFlex);
	assert(c_FlexPoint_Gej & pFlex->m_Flags);
	assert(!secp256k1_gej_is_infinity(&pFlex->m_Gej));

	secp256k1_gej pOdds[c_MultiMac_Fast_nCount];
	pOdds[0] = pFlex->m_Gej;

	// calculate odd powers
	secp256k1_gej x2;
	secp256k1_gej_double_var(&x2, pOdds, 0);

	for (unsigned int i = 1; i < c_MultiMac_Fast_nCount; i++)
	{
		secp256k1_gej_add_var(pOdds + i, pOdds + i - 1, &x2, 0);
		assert(!secp256k1_gej_is_infinity(pOdds + i)); // odd powers of non-zero point must not be zero!
	}

	secp256k1_fe pFe[c_MultiMac_Fast_nCount];

	ToCommonDenominator(c_MultiMac_Fast_nCount, pOdds, pFe, p->m_pZDenom, 0);

	for (unsigned int i = 0; i < c_MultiMac_Fast_nCount; i++)
	{
		secp256k1_ge ge;
		secp256k1_ge_set_gej_normalized(&ge, pOdds + i);
		secp256k1_ge_to_storage((secp256k1_ge_storage*)p->m_pGenFast[0].m_pPt + i, &ge);
	}
}

//////////////////////////////
// NonceGenerator
void NonceGenerator_InitBegin(NonceGenerator* p, secp256k1_hmac_sha256_t* pHMac, const char* szSalt, size_t nSalt)
{
	p->m_Counter = 0;
	p->m_FirstTime = 1;
	p->m_pContext = 0;
	p->m_nContext = 0;

	secp256k1_hmac_sha256_initialize(pHMac, (uint8_t*) szSalt, nSalt);
}

void NonceGenerator_InitEnd(NonceGenerator* p, secp256k1_hmac_sha256_t* pHMac)
{
	secp256k1_hmac_sha256_finalize(pHMac, p->m_Prk.m_pVal);
}

__stack_hungry__
void NonceGenerator_Init(NonceGenerator* p, const char* szSalt, size_t nSalt, const UintBig* pSeed)
{
	secp256k1_hmac_sha256_t hmac;

	NonceGenerator_InitBegin(p, &hmac, szSalt, nSalt);
	secp256k1_hmac_sha256_write(&hmac, pSeed->m_pVal, sizeof(pSeed->m_pVal));
	NonceGenerator_InitEnd(p, &hmac);
}

__stack_hungry__
void NonceGenerator_NextOkm(NonceGenerator* p)
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

void NonceGenerator_NextScalar(NonceGenerator* p, secp256k1_scalar* pS)
{
	while (1)
	{
		NonceGenerator_NextOkm(p);
		if (ScalarImportNnz(pS, p->m_Okm.m_pVal))
			break;
	}
}

int memis0(const uint8_t* p, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		if (p[i])
			return 0;
	return 1;
}

static int IsUintBigZero(const UintBig* p)
{
	return memis0(p->m_pVal, sizeof(p->m_pVal));
}

//////////////////////////////
// Point
void FlexPoint_MakeCompact(FlexPoint* pFlex)
{
	if ((c_FlexPoint_Compact & pFlex->m_Flags) || !pFlex->m_Flags)
		return;

	FlexPoint_MakeGe(pFlex);
	assert(c_FlexPoint_Ge & pFlex->m_Flags);

	if (secp256k1_ge_is_infinity(&pFlex->m_Ge))
		memset(&pFlex->m_Compact, 0, sizeof(pFlex->m_Compact));
	else
	{
		secp256k1_fe_normalize(&pFlex->m_Ge.x);
		secp256k1_fe_normalize(&pFlex->m_Ge.y);

		secp256k1_fe_get_b32(pFlex->m_Compact.m_X.m_pVal, &pFlex->m_Ge.x);
		pFlex->m_Compact.m_Y = (secp256k1_fe_is_odd(&pFlex->m_Ge.y) != 0);
	}

	pFlex->m_Flags |= c_FlexPoint_Compact;
}

void FlexPoint_MakeGej(FlexPoint* pFlex)
{
	if (c_FlexPoint_Gej & pFlex->m_Flags)
		return;

	FlexPoint_MakeGe(pFlex);
	if (!pFlex->m_Flags)
		return;

	assert(c_FlexPoint_Ge & pFlex->m_Flags);
	secp256k1_gej_set_ge(&pFlex->m_Gej, &pFlex->m_Ge);

	pFlex->m_Flags |= c_FlexPoint_Gej;
}

void FlexPoint_MakeGe(FlexPoint* pFlex)
{
	if (c_FlexPoint_Ge & pFlex->m_Flags)
		return;

	if (c_FlexPoint_Gej & pFlex->m_Flags)
		secp256k1_ge_set_gej_var(&pFlex->m_Ge, &pFlex->m_Gej); // expensive, better to a batch convertion
	else
	{
		if (!(c_FlexPoint_Compact & pFlex->m_Flags))
			return;

		pFlex->m_Flags = 0; // will restore Compact flag iff import is successful

		if (pFlex->m_Compact.m_Y > 1)
			return; // not well-formed

		// use pFlex->m_Gej.x as a temp var
		if (!secp256k1_fe_set_b32(&pFlex->m_Gej.x, pFlex->m_Compact.m_X.m_pVal))
			return; // not well-formed

		if (!secp256k1_ge_set_xo_var(&pFlex->m_Ge, &pFlex->m_Gej.x, pFlex->m_Compact.m_Y))
		{
			// be convention zeroed Compact is a zero point
			if (pFlex->m_Compact.m_Y || !IsUintBigZero(&pFlex->m_Compact.m_X))
				return;

			pFlex->m_Ge.infinity = 1; // no specific function like secp256k1_ge_set_infinity
		}

		pFlex->m_Flags = c_FlexPoint_Compact; // restored
	}

	pFlex->m_Flags |= c_FlexPoint_Ge;
}

__stack_hungry__
void FlexPoint_MakeGe_Batch(FlexPoint* pFlex, unsigned int nCount)
{
	assert(nCount);

	static_assert(sizeof(pFlex->m_Ge) >= sizeof(secp256k1_fe), "Ge is used as a temp placeholder for Fe");
#define FLEX_POINT_TEMP_FE(pt) ((secp256k1_fe*) (&(pt).m_Ge))

	for (unsigned int i = 0; i < nCount; i++)
	{
		assert(c_FlexPoint_Gej == pFlex[i].m_Flags);

		BatchNormalize_Fwd(FLEX_POINT_TEMP_FE(pFlex[i]), i, &pFlex[i].m_Gej, FLEX_POINT_TEMP_FE(pFlex[i - 1]));
	}

	secp256k1_fe zDenom;
	BatchNormalize_Apex(&zDenom, FLEX_POINT_TEMP_FE(pFlex[nCount - 1]), 1);

	for (unsigned int i = nCount; i--; )
	{
		BatchNormalize_Bwd(FLEX_POINT_TEMP_FE(pFlex[i]), i, &pFlex[i].m_Gej, FLEX_POINT_TEMP_FE(pFlex[i - 1]), &zDenom);

		secp256k1_ge_set_gej_normalized(&pFlex[i].m_Ge, &pFlex[i].m_Gej);
		pFlex[i].m_Flags = c_FlexPoint_Ge;
	}

	assert(nCount);
}

void MulPoint(FlexPoint* pFlex, const MultiMac_Secure* pGen, const secp256k1_scalar* pK)
{
	MultiMac_Context ctx;
	ctx.m_pRes = &pFlex->m_Gej;
	ctx.m_pZDenom = 0;
	ctx.m_Fast = 0;
	ctx.m_Secure = 1;
	ctx.m_pGenSecure = pGen;
	ctx.m_pSecureK = pK;

	MultiMac_Calculate(&ctx);
	pFlex->m_Flags = c_FlexPoint_Gej;
}

void MulG(FlexPoint* pFlex, const secp256k1_scalar* pK)
{
	MulPoint(pFlex, Context_get()->m_pGenGJ, pK);
}

__stack_hungry__
void Sk2Pk(UintBig* pRes, secp256k1_scalar* pK)
{
	FlexPoint fp;
	MulG(&fp, pK);

	FlexPoint_MakeCompact(&fp);
	assert(c_FlexPoint_Compact & fp.m_Flags);

	*pRes = fp.m_Compact.m_X;

	if (fp.m_Compact.m_Y)
		secp256k1_scalar_negate(pK, pK);
}
//////////////////////////////
// Oracle
void Oracle_Init(Oracle* p)
{
	secp256k1_sha256_initialize(&p->m_sha);
}

void Oracle_Expose(Oracle* p, const uint8_t* pPtr, size_t nSize)
{
	secp256k1_sha256_write(&p->m_sha, pPtr, nSize);
}

__stack_hungry__
void Oracle_NextHash(Oracle* p, UintBig* pHash)
{
	secp256k1_sha256_t sha = p->m_sha; // copy
	secp256k1_sha256_finalize(&sha, pHash->m_pVal);

	secp256k1_sha256_write(&p->m_sha, pHash->m_pVal, c_ECC_nBytes);
}

__stack_hungry__
void Oracle_NextScalar(Oracle* p, secp256k1_scalar* pS)
{
	while (1)
	{
		UintBig hash;
		Oracle_NextHash(p, &hash);

		if (ScalarImportNnz(pS, hash.m_pVal))
			break;
	}
}

void Oracle_NextPoint(Oracle* p, FlexPoint* pFlex)
{
	pFlex->m_Compact.m_Y = 0;

	while (1)
	{
		Oracle_NextHash(p, &pFlex->m_Compact.m_X);
		pFlex->m_Flags = c_FlexPoint_Compact;

		FlexPoint_MakeGe(pFlex);

		if ((c_FlexPoint_Ge & pFlex->m_Flags) && !secp256k1_ge_is_infinity(&pFlex->m_Ge))
			break;
	}
}

//////////////////////////////
// CoinID
#define c_CoinID_nSubkeyBits 24

int CoinID_getSchemeAndSubkey(const CoinID* p, uint8_t* pScheme, uint32_t* pSubkey)
{
	*pScheme = (uint8_t) (p->m_SubIdx >> c_CoinID_nSubkeyBits);
	*pSubkey = p->m_SubIdx & ((1U << c_CoinID_nSubkeyBits) - 1);

	if (!*pSubkey)
		return 0; // by convention: up to latest scheme, Subkey=0 - is a master key

	if (c_CoinID_Scheme_BB21 == *pScheme)
		return 0; // BB2.1 workaround

	return 1;
}

#define HASH_WRITE_STR(hash, str) secp256k1_sha256_write(&(hash), (const unsigned char*)str, sizeof(str))

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

void secp256k1_sha256_write_CompactPoint(secp256k1_sha256_t* pSha, const CompactPoint* pCompact)
{
	secp256k1_sha256_write(pSha, pCompact->m_X.m_pVal, sizeof(pCompact->m_X.m_pVal));
	secp256k1_sha256_write(pSha, &pCompact->m_Y, sizeof(pCompact->m_Y));
}

void secp256k1_sha256_write_CompactPointOptional2(secp256k1_sha256_t* pSha, const CompactPoint* pCompact, uint8_t bValid)
{
	secp256k1_sha256_write(pSha, &bValid, sizeof(bValid));
	if (bValid)
		secp256k1_sha256_write_CompactPoint(pSha, pCompact);
}

void secp256k1_sha256_write_CompactPointOptional(secp256k1_sha256_t* pSha, const CompactPoint* pCompact)
{
	secp256k1_sha256_write_CompactPointOptional2(pSha, pCompact, !!pCompact);
}

void secp256k1_sha256_write_CompactPointEx(secp256k1_sha256_t* pSha, const UintBig* pX, uint8_t nY)
{
	secp256k1_sha256_write(pSha, pX->m_pVal, sizeof(pX->m_pVal));

	nY &= 1;
	secp256k1_sha256_write(pSha, &nY, sizeof(nY));
}

void secp256k1_sha256_write_Point(secp256k1_sha256_t* pSha, FlexPoint* pFlex)
{
	FlexPoint_MakeCompact(pFlex);
	assert(c_FlexPoint_Compact & pFlex->m_Flags);
	secp256k1_sha256_write_CompactPoint(pSha, &pFlex->m_Compact);
}

__stack_hungry__
void CoinID_getHash(const CoinID* p, UintBig* pHash)
{
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);

	uint8_t nScheme;
	uint32_t nSubkey;
	CoinID_getSchemeAndSubkey(p, &nScheme, &nSubkey);

	uint32_t nSubIdx = p->m_SubIdx;

	switch (nScheme)
	{
	case c_CoinID_Scheme_BB21:
		// this is actually V0, with a workaround
		nSubIdx = nSubkey | (c_CoinID_Scheme_V0 << c_CoinID_nSubkeyBits);
		nScheme = c_CoinID_Scheme_V0;
		// no break;

	case c_CoinID_Scheme_V0:
		HASH_WRITE_STR(sha, "kid");
		break;

	default:
		HASH_WRITE_STR(sha, "kidv-1");
	}

	secp256k1_sha256_write_Num(&sha, p->m_Idx);
	secp256k1_sha256_write_Num(&sha, p->m_Type);
	secp256k1_sha256_write_Num(&sha, nSubIdx);

	if (nScheme >= c_CoinID_Scheme_V1)
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
__stack_hungry__
void Kdf_Init(Kdf* p, const UintBig* pSeed)
{
	static const char szSalt[] = "beam-HKdf";

	NonceGenerator ng;
	NonceGenerator_Init(&ng, szSalt, sizeof(szSalt), pSeed);

	static const char szCtx1[] = "gen";
	static const char szCtx2[] = "coF";

	ng.m_pContext = (const uint8_t*) szCtx1;
	ng.m_nContext = sizeof(szCtx1);

	NonceGenerator_NextOkm(&ng);
	p->m_Secret = ng.m_Okm;

	ng.m_Counter = 0;
	ng.m_FirstTime = 1;
	ng.m_pContext = (const uint8_t*) szCtx2;
	ng.m_nContext = sizeof(szCtx2);
	NonceGenerator_NextScalar(&ng, &p->m_kCoFactor);

	SECURE_ERASE_OBJ(ng);
}

__stack_hungry__
void Kdf_Derive_PKey_Pre(const Kdf* p, const UintBig* pHv, NonceGenerator* pN)
{
	static const char szSalt[] = "beam-Key";

	secp256k1_hmac_sha256_t hmac;
	NonceGenerator_InitBegin(pN, &hmac, szSalt, sizeof(szSalt));

	secp256k1_hmac_sha256_write(&hmac, p->m_Secret.m_pVal, sizeof(p->m_Secret.m_pVal));
	secp256k1_hmac_sha256_write(&hmac, pHv->m_pVal, sizeof(pHv->m_pVal));

	NonceGenerator_InitEnd(pN, &hmac);

	SECURE_ERASE_OBJ(hmac);
}

__stack_hungry__
void Kdf_Derive_PKey(const Kdf* p, const UintBig* pHv, secp256k1_scalar* pK)
{
	NonceGenerator ng;
	Kdf_Derive_PKey_Pre(p, pHv, &ng);

	NonceGenerator_NextScalar(&ng, pK);

	SECURE_ERASE_OBJ(ng);
}

void Kdf_Derive_SKey(const Kdf* p, const UintBig* pHv, secp256k1_scalar* pK)
{
	Kdf_Derive_PKey(p, pHv, pK);
	secp256k1_scalar_mul(pK, pK, &p->m_kCoFactor);
}

#define ARRAY_ELEMENT_SAFE(arr, index) ((arr)[(((index) < _countof(arr)) ? (index) : (_countof(arr) - 1))])
#define FOURCC_FROM_BYTES(a, b, c, d) (((((((uint32_t) a << 8) | (uint32_t) b) << 8) | (uint32_t) c) << 8) | (uint32_t) d)
#define FOURCC_FROM_STR(name) FOURCC_FROM_BYTES(ARRAY_ELEMENT_SAFE(#name,0), ARRAY_ELEMENT_SAFE(#name,1), ARRAY_ELEMENT_SAFE(#name,2), ARRAY_ELEMENT_SAFE(#name,3))

__stack_hungry__
void Kdf_getChild_Hv(uint32_t iChild, UintBig* pHv)
{
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);
	HASH_WRITE_STR(sha, "kid");

	const uint32_t nType = FOURCC_FROM_STR(SubK);

	secp256k1_sha256_write_Num(&sha, iChild);
	secp256k1_sha256_write_Num(&sha, nType);
	secp256k1_sha256_write_Num(&sha, 0);

	secp256k1_sha256_finalize(&sha, pHv->m_pVal);
}

__stack_hungry__
void Kdf_getChild_Hv2(const Kdf* pParent, uint32_t iChild, UintBig* pHv)
{
	Kdf_getChild_Hv(iChild, pHv);

	secp256k1_scalar sk;
	Kdf_Derive_SKey(pParent, pHv, &sk);

	secp256k1_scalar_get_b32(pHv->m_pVal, &sk);
	SECURE_ERASE_OBJ(sk);
}

__stack_hungry__
void Kdf_getChild(Kdf* p, uint32_t iChild, const Kdf* pParent)
{
	UintBig hv;
	Kdf_getChild_Hv2(pParent, iChild, &hv);

	Kdf_Init(p, &hv);
	SECURE_ERASE_OBJ(hv);
}

//////////////////////////////
// Kdf - CoinID key derivation
//__stack_hungry__
void CoinID_getCommRawEx(const secp256k1_scalar* pkG, const secp256k1_scalar* pkH, AssetID aid, FlexPoint* pComm)
{
	union
	{
		// save some space
		struct
		{
			Oracle oracle;
		} o;

		struct
		{
			secp256k1_scalar s;
			MultiMac_WNaf wnaf;
			MultiMac_Fast genAsset;
			secp256k1_fe zDenom;
		} mm;

	} u;

	Context* pCtx = Context_get();

	// sk*G + v*H
	MultiMac_Context mmCtx;
	mmCtx.m_pRes = &pComm->m_Gej;
	mmCtx.m_Secure = 1;
	mmCtx.m_pSecureK = pkG;
	mmCtx.m_pGenSecure = pCtx->m_pGenGJ;
	mmCtx.m_Fast = 1;
	mmCtx.m_pFastK = &u.mm.s;
	mmCtx.m_pWnaf = &u.mm.wnaf;

	if (aid)
	{
		// derive asset gen
		Oracle_Init(&u.o.oracle);

		HASH_WRITE_STR(u.o.oracle.m_sha, "B.Asset.Gen.V1");
		secp256k1_sha256_write_Num(&u.o.oracle.m_sha, aid);

		Oracle_NextPoint(&u.o.oracle, pComm);

		mmCtx.m_pGenFast = &u.mm.genAsset;
		mmCtx.m_pZDenom = &u.mm.zDenom;

		MultiMac_SetCustom_Nnz(&mmCtx, pComm);

	}
	else
	{
		mmCtx.m_pGenFast = pCtx->m_pGenFast + c_MultiMac_Fast_Idx_H;
		mmCtx.m_pZDenom = 0;
	}

	u.mm.s = *pkH; // copy

	MultiMac_Calculate(&mmCtx);
	pComm[0].m_Flags = c_FlexPoint_Gej;
}

//__stack_hungry__
void CoinID_getCommRaw(const secp256k1_scalar* pK, Amount amount, AssetID aid, FlexPoint* pComm)
{
	secp256k1_scalar kH;
	secp256k1_scalar_set_u64(&kH, amount);
	CoinID_getCommRawEx(pK, &kH, aid, pComm);
}

void CoinID_getSk(const Kdf* pKdf, const CoinID* pCid, secp256k1_scalar* pK)
{
	CoinID_getSkComm(pKdf, pCid, pK, 0);
}

__stack_hungry__
static void CoinID_getSkNonSwitch(const Kdf* pKdf, const CoinID* pCid, secp256k1_scalar* pK)
{
	uint8_t nScheme;
	uint32_t nSubkey;
	UintBig hv;
	Kdf kdfC;

	int nChild = CoinID_getSchemeAndSubkey(pCid, &nScheme, &nSubkey);
	if (nChild)
	{
		Kdf_getChild(&kdfC, nSubkey, pKdf);
		pKdf = &kdfC;
	}

	CoinID_getHash(pCid, &hv);
	Kdf_Derive_SKey(pKdf, &hv, pK);

	if (nChild)
		SECURE_ERASE_OBJ(kdfC);
}

__stack_hungry__
static void CoinID_getSkSwitchDelta(secp256k1_scalar* pK, FlexPoint* pFlex)
{
	Oracle oracle;

	Oracle_Init(&oracle);
	secp256k1_sha256_write_Point(&oracle.m_sha, pFlex);
	secp256k1_sha256_write_Point(&oracle.m_sha, pFlex + 1);

	Oracle_NextScalar(&oracle, pK);
}

__stack_hungry__
static void CoinID_getSkComm_FromNonSwitchK(const CoinID* pCid, secp256k1_scalar* pK, CompactPoint* pComm)
{
	FlexPoint pFlex[2];

	CoinID_getCommRaw(pK, pCid->m_Amount, pCid->m_AssetID, pFlex); // sk*G + amount*H(aid)
	MulPoint(pFlex + 1, Context_get()->m_pGenGJ + 1, pK); // sk*J

	FlexPoint_MakeGe_Batch(pFlex, _countof(pFlex));

	secp256k1_scalar kDelta;
	CoinID_getSkSwitchDelta(&kDelta, pFlex);

	secp256k1_scalar_add(pK, pK, &kDelta);

	if (pComm)
	{
		MulG(pFlex + 1, &kDelta);

		assert(c_FlexPoint_Gej & pFlex[1].m_Flags);
		assert(c_FlexPoint_Ge & pFlex[0].m_Flags);

		secp256k1_gej_add_ge_var(&pFlex[0].m_Gej, &pFlex[1].m_Gej, &pFlex[0].m_Ge, 0);
		pFlex[0].m_Flags = c_FlexPoint_Gej;

		FlexPoint_MakeCompact(&pFlex[0]);
		assert(c_FlexPoint_Compact && pFlex[0].m_Flags);
		*pComm = pFlex[0].m_Compact;
	}
}

void CoinID_getSkComm(const Kdf* pKdf, const CoinID* pCid, secp256k1_scalar* pK, CompactPoint* pComm)
{
	CoinID_getSkNonSwitch(pKdf, pCid, pK);
	CoinID_getSkComm_FromNonSwitchK(pCid, pK, pComm);
}

__stack_hungry__
static void ShieldedInput_getSk(const KeyKeeper* p, const ShieldedInput* pInp, secp256k1_scalar* pK)
{
	UintBig hv;
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

	Kdf kdfChild;
	Kdf_getChild(&kdfChild, c_ShieldedInput_ChildKdf, &p->m_MasterKey);
	Kdf_Derive_SKey(&kdfChild, &hv, pK);
}

//////////////////////////////
// RangeProof

static void WriteInNetworkOrderRaw(uint8_t* pDst, uint64_t val, unsigned int nLen)
{
	for (unsigned int i = nLen; i--; val >>= 8)
		pDst[i] = (uint8_t) val;
}

static void WriteInNetworkOrder(uint8_t** ppDst, uint64_t val, unsigned int nLen)
{
	*ppDst -= nLen;
	WriteInNetworkOrderRaw(*ppDst, val, nLen);
}

static uint64_t ReadInNetworkOrder(const uint8_t* pSrc, unsigned int nLen)
{
	assert(nLen);
	uint64_t ret = pSrc[0];

	for (unsigned int i = 1; i < nLen; i++)
		ret = (ret << 8) | pSrc[i];

	return ret;
}

typedef struct
{
	NonceGenerator m_NonceGen; // 88 bytes
	secp256k1_gej m_pGej[2]; // 248 bytes

	// 97 bytes. This can be saved, at expense of calculating them again (CoinID_getSkComm)
	secp256k1_scalar m_sk;
	secp256k1_scalar m_alpha;
	CompactPoint m_Commitment;

	union
	{
		struct
		{
			FlexPoint fp;
			Oracle oracle;
			UintBig hv;
			secp256k1_scalar k;
		} p1;

		struct
		{
			// Data buffers needed for calculating Part1.S
			// Need to multi-exponentiate nDims * 2 == 128 elements.
			// Calculating everything in a single pass is faster, but requires more buffers (stack memory)
			// Each element size is sizeof(secp256k1_scalar) + sizeof(MultiMac_WNaf) == 34 bytes
			//
			// This requires of 4.25K stack memory
#define nDims (sizeof(Amount) * 8)
#define Calc_S_Naggle_Max (nDims * 2)

#define Calc_S_Naggle Calc_S_Naggle_Max // currently using max

			secp256k1_scalar pS[Calc_S_Naggle];
			MultiMac_WNaf pWnaf[Calc_S_Naggle];

			secp256k1_scalar ro;

		} p2;

		struct
		{
			secp256k1_scalar pK[2];

			Oracle oracle;
			secp256k1_scalar pChallenge[2];
			UintBig hv;
			secp256k1_hmac_sha256_t hmac;

			FlexPoint pFp[2]; // 496 bytes

		} p3;

	} u;

} RangeProof_Worker;


static void RangeProof_Calculate_Before_S(RangeProof* const p, RangeProof_Worker* const pWrk)
{
	CoinID_getSkComm(p->m_pKdf, &p->m_Cid, &pWrk->m_sk, &pWrk->u.p1.fp.m_Compact);
	pWrk->u.p1.fp.m_Flags = c_FlexPoint_Compact;

	pWrk->m_Commitment = pWrk->u.p1.fp.m_Compact;

	// get seed
	secp256k1_sha256_initialize(&pWrk->u.p1.oracle.m_sha);
	secp256k1_sha256_write_Point(&pWrk->u.p1.oracle.m_sha, &pWrk->u.p1.fp);

	secp256k1_sha256_finalize(&pWrk->u.p1.oracle.m_sha, pWrk->u.p1.hv.m_pVal);

	Kdf_Derive_PKey(p->m_pKdf, &pWrk->u.p1.hv, &pWrk->u.p1.k);
	secp256k1_scalar_get_b32(pWrk->u.p1.hv.m_pVal, &pWrk->u.p1.k);

	secp256k1_sha256_initialize(&pWrk->u.p1.oracle.m_sha);
	secp256k1_sha256_write(&pWrk->u.p1.oracle.m_sha, pWrk->u.p1.hv.m_pVal, sizeof(pWrk->u.p1.hv.m_pVal));
	secp256k1_sha256_finalize(&pWrk->u.p1.oracle.m_sha, pWrk->u.p1.hv.m_pVal);

	// NonceGen
	static const char szSalt[] = "bulletproof";
	NonceGenerator_Init(&pWrk->m_NonceGen, szSalt, sizeof(szSalt), &pWrk->u.p1.hv);

	NonceGenerator_NextScalar(&pWrk->m_NonceGen, &pWrk->m_alpha); // alpha

	// embed params into alpha
	uint8_t* pPtr = pWrk->u.p1.hv.m_pVal + c_ECC_nBytes;
	WriteInNetworkOrder(&pPtr, p->m_Cid.m_Amount, sizeof(p->m_Cid.m_Amount));
	WriteInNetworkOrder(&pPtr, p->m_Cid.m_SubIdx, sizeof(p->m_Cid.m_SubIdx));
	WriteInNetworkOrder(&pPtr, p->m_Cid.m_Type, sizeof(p->m_Cid.m_Type));
	WriteInNetworkOrder(&pPtr, p->m_Cid.m_Idx, sizeof(p->m_Cid.m_Idx));
	WriteInNetworkOrder(&pPtr, p->m_Cid.m_AssetID, sizeof(p->m_Cid.m_AssetID));
	memset(pWrk->u.p1.hv.m_pVal, 0, pPtr - pWrk->u.p1.hv.m_pVal); // padding

	int overflow;
	secp256k1_scalar_set_b32(&pWrk->u.p1.k, pWrk->u.p1.hv.m_pVal, &overflow);
	assert(!overflow);

	secp256k1_scalar_add(&pWrk->m_alpha, &pWrk->m_alpha, &pWrk->u.p1.k);
}


static void RangeProof_Calculate_S(RangeProof* const p, RangeProof_Worker* const pWrk)
{
	static_assert(Calc_S_Naggle <= Calc_S_Naggle_Max, "Naggle too large");

	// Try to avoid local vars, save as much stack as possible

	NonceGenerator_NextScalar(&pWrk->m_NonceGen, &pWrk->u.p2.ro);

	MultiMac_Context mmCtx;
	mmCtx.m_pZDenom = 0;

	mmCtx.m_Secure = 1;
	mmCtx.m_pSecureK = &pWrk->u.p2.ro;
	mmCtx.m_pGenSecure = Context_get()->m_pGenGJ;

	mmCtx.m_Fast = 0;
	mmCtx.m_pGenFast = Context_get()->m_pGenFast;
	mmCtx.m_pFastK = pWrk->u.p2.pS;
	mmCtx.m_pWnaf = pWrk->u.p2.pWnaf;

	for (unsigned int iBit = 0; iBit < nDims * 2; iBit++, mmCtx.m_Fast++)
	{
		if (Calc_S_Naggle == mmCtx.m_Fast)
		{
			// flush
			mmCtx.m_pRes = pWrk->m_pGej + (iBit != Calc_S_Naggle); // 1st flush goes to pGej[0] directly
			MultiMac_Calculate(&mmCtx);

			if (iBit != Calc_S_Naggle)
				secp256k1_gej_add_var(pWrk->m_pGej, pWrk->m_pGej + 1, pWrk->m_pGej, 0);

			mmCtx.m_Secure = 0;
			mmCtx.m_Fast = 0;
			mmCtx.m_pGenFast += Calc_S_Naggle;
		}

		NonceGenerator_NextScalar(&pWrk->m_NonceGen, pWrk->u.p2.pS + mmCtx.m_Fast);

		if (!(iBit % nDims) && p->m_pKExtra)
			// embed more info
			secp256k1_scalar_add(pWrk->u.p2.pS + mmCtx.m_Fast, pWrk->u.p2.pS + mmCtx.m_Fast, p->m_pKExtra + (iBit / nDims));
	}

	mmCtx.m_pRes = pWrk->m_pGej + 1;
	MultiMac_Calculate(&mmCtx);

	if (Calc_S_Naggle < Calc_S_Naggle_Max)
		secp256k1_gej_add_var(pWrk->m_pGej + 1, pWrk->m_pGej + 1, pWrk->m_pGej, 0);
}

static void RangeProof_Calculate_A_Bits(secp256k1_gej* pRes, secp256k1_ge* pGeTmp, Amount v)
{
	Context* pCtx = Context_get();
	for (uint32_t i = 0; i < nDims; i++)
	{
		if (1 & (v >> i))
			secp256k1_ge_from_storage(pGeTmp, pCtx->m_pGenFast[i].m_pPt);
		else
		{
			secp256k1_ge_from_storage(pGeTmp, pCtx->m_pGenFast[nDims + i].m_pPt);
			secp256k1_ge_neg(pGeTmp, pGeTmp);
		}

		secp256k1_gej_add_ge_var(pRes, pRes, pGeTmp, 0);
	}
}

static int RangeProof_Calculate_After_S(RangeProof* const p, RangeProof_Worker* const pWrk)
{
	Context* pCtx = Context_get();

	pWrk->u.p3.pFp[1].m_Gej = pWrk->m_pGej[1];
	pWrk->u.p3.pFp[1].m_Flags = c_FlexPoint_Gej;

	// CalcA
	MultiMac_Context mmCtx;
	mmCtx.m_pZDenom = 0;
	mmCtx.m_Fast = 0;
	mmCtx.m_Secure = 1;
	mmCtx.m_pSecureK = &pWrk->m_alpha;
	mmCtx.m_pGenSecure = pCtx->m_pGenGJ;
	mmCtx.m_pRes = &pWrk->u.p3.pFp[0].m_Gej;

	MultiMac_Calculate(&mmCtx); // alpha*G
	pWrk->u.p3.pFp[0].m_Flags = c_FlexPoint_Gej;

	RangeProof_Calculate_A_Bits(&pWrk->u.p3.pFp[0].m_Gej, &pWrk->u.p3.pFp[0].m_Ge, p->m_Cid.m_Amount);


	// normalize A,S at once, feed them to Oracle
	FlexPoint_MakeGe_Batch(pWrk->u.p3.pFp, _countof(pWrk->u.p3.pFp));

	Oracle_Init(&pWrk->u.p3.oracle);
	secp256k1_sha256_write_Num(&pWrk->u.p3.oracle.m_sha, 0); // incubation time, must be zero
	secp256k1_sha256_write_CompactPoint(&pWrk->u.p3.oracle.m_sha, &pWrk->m_Commitment); // starting from Fork1, earlier schem is not allowed
	secp256k1_sha256_write_CompactPointOptional(&pWrk->u.p3.oracle.m_sha, p->m_pAssetGen); // starting from Fork3, earlier schem is not allowed

	for (unsigned int i = 0; i < 2; i++)
		secp256k1_sha256_write_Point(&pWrk->u.p3.oracle.m_sha, pWrk->u.p3.pFp + i);

	// get challenges. Use the challenges, sk, T1 and T2 to init the NonceGen for blinding the sk
	static const char szSalt[] = "bulletproof-sk";
	NonceGenerator_InitBegin(&pWrk->m_NonceGen, &pWrk->u.p3.hmac, szSalt, sizeof(szSalt));

	secp256k1_scalar_get_b32(pWrk->u.p3.hv.m_pVal, &pWrk->m_sk);
	secp256k1_hmac_sha256_write(&pWrk->u.p3.hmac, pWrk->u.p3.hv.m_pVal, sizeof(pWrk->u.p3.hv.m_pVal));

	for (unsigned int i = 0; i < 2; i++)
	{
		secp256k1_hmac_sha256_write(&pWrk->u.p3.hmac, p->m_pT_In[i].m_X.m_pVal, sizeof(p->m_pT_In[i].m_X.m_pVal));
		secp256k1_hmac_sha256_write(&pWrk->u.p3.hmac, &p->m_pT_In[i].m_Y, sizeof(p->m_pT_In[i].m_Y));

		Oracle_NextScalar(&pWrk->u.p3.oracle, pWrk->u.p3.pChallenge); // challenges y,z. The 'y' is not needed, will be overwritten by 'z'.
		secp256k1_scalar_get_b32(pWrk->u.p3.hv.m_pVal, pWrk->u.p3.pChallenge);
		secp256k1_hmac_sha256_write(&pWrk->u.p3.hmac, pWrk->u.p3.hv.m_pVal, sizeof(pWrk->u.p3.hv.m_pVal));
	}

	int ok = 1;

	NonceGenerator_InitEnd(&pWrk->m_NonceGen, &pWrk->u.p3.hmac);

	for (unsigned int i = 0; i < 2; i++)
	{
		NonceGenerator_NextScalar(&pWrk->m_NonceGen, pWrk->u.p3.pK + i); // tau1/2
		mmCtx.m_pSecureK = pWrk->u.p3.pK + i;
		mmCtx.m_pRes = pWrk->m_pGej;

		MultiMac_Calculate(&mmCtx); // pub nonces of T1/T2

		pWrk->u.p3.pFp[i].m_Compact = p->m_pT_In[i];
		pWrk->u.p3.pFp[i].m_Flags = c_FlexPoint_Compact;
		FlexPoint_MakeGe(pWrk->u.p3.pFp + i);
		if (!pWrk->u.p3.pFp[i].m_Flags)
		{
			ok = 0;
			break;
		}

		secp256k1_gej_add_ge_var(&pWrk->u.p3.pFp[i].m_Gej, mmCtx.m_pRes, &pWrk->u.p3.pFp[i].m_Ge, 0);
		pWrk->u.p3.pFp[i].m_Flags = c_FlexPoint_Gej;
	}

	SECURE_ERASE_OBJ(pWrk->m_NonceGen);

	if (ok)
	{
		// normalize & expose
		FlexPoint_MakeGe_Batch(pWrk->u.p3.pFp, _countof(pWrk->u.p3.pFp));

		for (unsigned int i = 0; i < 2; i++)
		{
			secp256k1_sha256_write_Point(&pWrk->u.p3.oracle.m_sha, pWrk->u.p3.pFp + i);
			assert(c_FlexPoint_Compact & pWrk->u.p3.pFp[i].m_Flags);
			p->m_pT_Out[i] = pWrk->u.p3.pFp[i].m_Compact;
		}

		// last challenge
		Oracle_NextScalar(&pWrk->u.p3.oracle, pWrk->u.p3.pChallenge + 1);

		// m_TauX = tau2*x^2 + tau1*x + sk*z^2
		secp256k1_scalar_mul(pWrk->u.p3.pK, pWrk->u.p3.pK, pWrk->u.p3.pChallenge + 1); // tau1*x
		secp256k1_scalar_mul(pWrk->u.p3.pChallenge + 1, pWrk->u.p3.pChallenge + 1, pWrk->u.p3.pChallenge + 1); // x^2
		secp256k1_scalar_mul(pWrk->u.p3.pK + 1, pWrk->u.p3.pK + 1, pWrk->u.p3.pChallenge + 1); // tau2*x^2

		secp256k1_scalar_mul(pWrk->u.p3.pChallenge, pWrk->u.p3.pChallenge, pWrk->u.p3.pChallenge); // z^2

		secp256k1_scalar_mul(p->m_pTauX, &pWrk->m_sk, pWrk->u.p3.pChallenge); // sk*z^2
		secp256k1_scalar_add(p->m_pTauX, p->m_pTauX, pWrk->u.p3.pK);
		secp256k1_scalar_add(p->m_pTauX, p->m_pTauX, pWrk->u.p3.pK + 1);
	}

	SECURE_ERASE_OBJ(pWrk->m_sk);
	SECURE_ERASE_OBJ(pWrk->u.p3.pK); // tau1/2
	//SECURE_ERASE_OBJ(hv); - no need, last value is the challenge

	return ok;
}

__stack_hungry__
int RangeProof_Calculate(RangeProof* p)
{
	RangeProof_Worker wrk;

	RangeProof_Calculate_Before_S(p, &wrk);
	RangeProof_Calculate_S(p, &wrk);
	return RangeProof_Calculate_After_S(p, &wrk);
}

typedef struct
{
	// in
	UintBig m_SeedGen;
	const UintBig* m_pSeedSk;
	uint32_t m_nUser; // has to be no bigger than c_ECC_nBytes - sizeof(Amount). If less - zero-padding is verified
	// out
	void* m_pUser;
	Amount m_Amount;

	secp256k1_scalar* m_pSk;
	secp256k1_scalar* m_pExtra;

} RangeProof_Recovery_Context;

__stack_hungry__
static int RangeProof_Recover(const RangeProof_Packed* pRangeproof, Oracle* pOracle, RangeProof_Recovery_Context* pCtx)
{
	static const char szSalt[] = "bulletproof";
	NonceGenerator ng;
	NonceGenerator_Init(&ng, szSalt, sizeof(szSalt), &pCtx->m_SeedGen);

	secp256k1_scalar alpha_minus_params, ro, x, y, z, tmp;
	NonceGenerator_NextScalar(&ng, &alpha_minus_params);
	NonceGenerator_NextScalar(&ng, &ro);

	// oracle << p1.A << p1.S
	// oracle >> y, z
	// oracle << p2.T1 << p2.T2
	// oracle >> x
	secp256k1_sha256_write_CompactPointEx(&pOracle->m_sha, &pRangeproof->m_Ax, pRangeproof->m_pYs[1] >> 4);
	secp256k1_sha256_write_CompactPointEx(&pOracle->m_sha, &pRangeproof->m_Sx, pRangeproof->m_pYs[1] >> 5);
	Oracle_NextScalar(pOracle, &y);
	Oracle_NextScalar(pOracle, &z);
	secp256k1_sha256_write_CompactPointEx(&pOracle->m_sha, &pRangeproof->m_T1x, pRangeproof->m_pYs[1] >> 6);
	secp256k1_sha256_write_CompactPointEx(&pOracle->m_sha, &pRangeproof->m_T2x, pRangeproof->m_pYs[1] >> 7);
	Oracle_NextScalar(pOracle, &x);

	// m_Mu = alpha + ro*x
	// alpha = m_Mu - ro*x = alpha_minus_params + params
	// params = m_Mu - ro*x - alpha_minus_params
	secp256k1_scalar_mul(&ro, &ro, &x);
	secp256k1_scalar_add(&tmp, &alpha_minus_params, &ro);
	secp256k1_scalar_negate(&tmp, &tmp); // - ro*x - alpha_minus_params

	int overflow;
	secp256k1_scalar_set_b32(&ro, pRangeproof->m_Mu.m_pVal, &overflow);
	if (overflow)
		return 0;

	secp256k1_scalar_add(&tmp, &tmp, &ro);

	{
		static_assert(sizeof(ro) >= c_ECC_nBytes, "");
		uint8_t* pBlob = (uint8_t*)&ro; // just reuse this mem

		secp256k1_scalar_get_b32(pBlob, &tmp);

		assert(pCtx->m_nUser <= c_ECC_nBytes - sizeof(Amount));
		uint32_t nPad = c_ECC_nBytes - sizeof(Amount) - pCtx->m_nUser;

		if (!memis0(pBlob, nPad))
			return 0;

		memcpy(pCtx->m_pUser, pBlob + nPad, pCtx->m_nUser);

		// recover value. It's always at the buf end
		pCtx->m_Amount = ReadInNetworkOrder(pBlob + c_ECC_nBytes - sizeof(Amount), sizeof(Amount));
	}

	secp256k1_scalar_add(&alpha_minus_params, &alpha_minus_params, &tmp); // just alpha

	// Recalculate p1.A, make sure we get the correct result
	FlexPoint comm;
	MulG(&comm, &alpha_minus_params);
	RangeProof_Calculate_A_Bits(&comm.m_Gej, &comm.m_Ge, pCtx->m_Amount);
	FlexPoint_MakeCompact(&comm);

	if (memcmp(comm.m_Compact.m_X.m_pVal, pRangeproof->m_Ax.m_pVal, c_ECC_nBytes) || (comm.m_Compact.m_Y != (1 & (pRangeproof->m_pYs[1] >> 4))))
		return 0; // false positive

	if (pCtx->m_pSeedSk || pCtx->m_pExtra)
		secp256k1_scalar_mul(&tmp, &z, &z); // z^2

	if (pCtx->m_pSeedSk)
	{
		assert(pCtx->m_pSk);

		secp256k1_scalar_set_b32(pCtx->m_pSk, pRangeproof->m_Taux.m_pVal, &overflow);

		// recover the blinding factor
		{
			static const char szSaltSk[] = "bp-key";
			NonceGenerator ngSk;
			NonceGenerator_Init(&ngSk, szSaltSk, sizeof(szSaltSk), pCtx->m_pSeedSk);
			NonceGenerator_NextScalar(&ngSk, &alpha_minus_params); // tau1
			NonceGenerator_NextScalar(&ngSk, &ro); // tau2
		}

		secp256k1_scalar_mul(&ro, &ro, &x);
		secp256k1_scalar_add(&ro, &ro, &alpha_minus_params);
		secp256k1_scalar_mul(&ro, &ro, &x); // tau2*x^2 + tau1*x

		secp256k1_scalar_negate(&ro, &ro);
		secp256k1_scalar_add(pCtx->m_pSk, pCtx->m_pSk, &ro);

		secp256k1_scalar_inverse(&ro, &tmp); // heavy operation
		secp256k1_scalar_mul(pCtx->m_pSk, pCtx->m_pSk, &ro);
	}

	if (pCtx->m_pExtra)
	{
		secp256k1_scalar pE[2][_countof(pRangeproof->m_pLRx)];

		secp256k1_sha256_write(&pOracle->m_sha, pRangeproof->m_tDot.m_pVal, sizeof(pRangeproof->m_tDot.m_pVal));
		Oracle_NextScalar(pOracle, &ro); // dot-multiplier, unneeded atm

		for (uint32_t iCycle = 0; iCycle < _countof(pRangeproof->m_pLRx); iCycle++)
		{
			Oracle_NextScalar(pOracle, pE[0] + iCycle); // challenge
			secp256k1_scalar_inverse(pE[1] + iCycle, pE[0] + iCycle);

			for (uint32_t j = 0; j < 2; j++)
			{
				uint32_t iBit = (iCycle << 1) + j;
				secp256k1_sha256_write_CompactPointEx(&pOracle->m_sha, pRangeproof->m_pLRx[iCycle] + j, pRangeproof->m_pYs[iBit >> 3] >> (7 & iBit));
			}
		}

		secp256k1_scalar yPwr;
		secp256k1_scalar_set_int(&yPwr, 1);
		secp256k1_scalar_set_int(&alpha_minus_params, 2);

		secp256k1_scalar_negate(&ro, &yPwr);
		secp256k1_scalar_add(&ro, &ro, &z);
		const secp256k1_scalar* pZ[] = { &z, &ro }; // z, z-1

		// tmp == z^2

		static_assert(!(nDims & 1), ""); // must be even
		secp256k1_scalar pS[nDims / 2]; // 32 elements, 1K stack size. Perform 1st condensation in-place (otherwise we'd need to prepare 64 elements first)

		for (unsigned int j = 0; j < 2; j++)
		{
			for (uint32_t i = 0; i < nDims; i++)
			{
				secp256k1_scalar val;
				NonceGenerator_NextScalar(&ng, &val);

				uint32_t bit = 1 & (pCtx->m_Amount >> i);
				secp256k1_scalar tmp2;

				if (j)
				{
					secp256k1_scalar_mul(&val, &val, &x); // pS[i] *= x;
					secp256k1_scalar_mul(&val, &val, &yPwr); // pS[i] *= yPwr;

					secp256k1_scalar_mul(&tmp2, pZ[!bit], &yPwr);
					secp256k1_scalar_add(&tmp2, &tmp2, &tmp);
					secp256k1_scalar_add(&val, &val, &tmp2); // pS[i] += pZ[!bit]*yPwr + z^2*2^i

					secp256k1_scalar_mul(&tmp, &tmp, &alpha_minus_params); // x2
					secp256k1_scalar_mul(&yPwr, &yPwr, &y);
				}
				else
				{
					secp256k1_scalar_mul(&val, &val, &x); // pS[i] *= x;

					secp256k1_scalar_negate(&tmp2, pZ[bit]);
					secp256k1_scalar_add(&val, &val, &tmp2); // pS[i] -= pZ[bit];
				}

				// 1st condensation in-place
				if (i < nDims / 2)
					secp256k1_scalar_mul(pS + i, &val, pE[j]);
				else
				{
					secp256k1_scalar_mul(&val, &val, pE[!j]);
					secp256k1_scalar_add(pS + i - nDims / 2, pS + i - nDims / 2, &val);
				}
			}

			// all other condensation cycles
			uint32_t nStep = nDims / 2;
			for (uint32_t iCycle = 1; iCycle < _countof(pRangeproof->m_pLRx); iCycle++)
			{
				nStep >>= 1;
				assert(nStep);

				for (uint32_t i = 0; i < nStep; i++)
				{
					secp256k1_scalar_mul(pS + i, pS + i, pE[j] + iCycle);
					secp256k1_scalar_mul(pS + nStep + i, pS + nStep + i, pE[!j] + iCycle);
					secp256k1_scalar_add(pS + i, pS + i, pS + nStep + i);
				}

			}
			assert(1 == nStep);

			secp256k1_scalar_set_b32(pS + 1, pRangeproof->m_pCondensed[j].m_pVal, &overflow);
			secp256k1_scalar_negate(pS, pS);
			secp256k1_scalar_add(pS, pS, pS + 1); // the difference

			// now let's estimate the difference that would be if extra == 1.
			pS[1] = x;
			for (uint32_t iCycle = 0; iCycle < _countof(pRangeproof->m_pLRx); iCycle++)
				secp256k1_scalar_mul(pS + 1, pS + 1, pE[j] + iCycle);

			secp256k1_scalar_inverse(pCtx->m_pExtra + j, pS + 1);
			secp256k1_scalar_mul(pCtx->m_pExtra + j, pCtx->m_pExtra + j, pS);
		}
	}

	return 1;
}

//////////////////////////////
// Signature
__stack_hungry__
void Signature_GetChallengeEx(const CompactPoint* pNoncePub, const UintBig* pMsg, secp256k1_scalar* pE)
{
	Oracle oracle;
	Oracle_Init(&oracle);
	secp256k1_sha256_write_CompactPoint(&oracle.m_sha, pNoncePub);
	secp256k1_sha256_write(&oracle.m_sha, pMsg->m_pVal, sizeof(pMsg->m_pVal));

	Oracle_NextScalar(&oracle, pE);
}

void Signature_GetChallenge(const Signature* p, const UintBig* pMsg, secp256k1_scalar* pE)
{
	Signature_GetChallengeEx(&p->m_NoncePub, pMsg, pE);
}

__stack_hungry__
void Signature_Sign(Signature* p, const UintBig* pMsg, const secp256k1_scalar* pSk)
{
	// get nonce
	secp256k1_hmac_sha256_t hmac;
	NonceGenerator ng;
	static const char szSalt[] = "beam-Schnorr";
	NonceGenerator_InitBegin(&ng, &hmac, szSalt, sizeof(szSalt));

	union
	{
		UintBig sk;
		secp256k1_scalar nonce;
	} u;

	static_assert(sizeof(u.nonce) >= sizeof(u.sk), ""); // means nonce completely overwrites the sk

	secp256k1_scalar_get_b32(u.sk.m_pVal, pSk);
	secp256k1_hmac_sha256_write(&hmac, u.sk.m_pVal, sizeof(u.sk.m_pVal));
	secp256k1_hmac_sha256_write(&hmac, pMsg->m_pVal, sizeof(pMsg->m_pVal));

	NonceGenerator_InitEnd(&ng, &hmac);
	NonceGenerator_NextScalar(&ng, &u.nonce);
	SECURE_ERASE_OBJ(ng);

	// expose the nonce
	FlexPoint fp;
	MulG(&fp, &u.nonce);
	FlexPoint_MakeCompact(&fp);
	p->m_NoncePub = fp.m_Compact;

	Signature_SignPartial(p, pMsg, pSk, &u.nonce);

	SECURE_ERASE_OBJ(u.nonce);
}

__stack_hungry__
void Signature_SignPartialEx(UintBig* pRes, const secp256k1_scalar* pE, const secp256k1_scalar* pSk, const secp256k1_scalar* pNonce)
{
	secp256k1_scalar k;
	secp256k1_scalar_mul(&k, pE, pSk);
	secp256k1_scalar_add(&k, &k, pNonce);
	secp256k1_scalar_negate(&k, &k);

	secp256k1_scalar_get_b32(pRes->m_pVal, &k);
}

__stack_hungry__
void Signature_SignPartial(Signature* p, const UintBig* pMsg, const secp256k1_scalar* pSk, const secp256k1_scalar* pNonce)
{
	secp256k1_scalar e;
	Signature_GetChallenge(p, pMsg, &e);
	Signature_SignPartialEx(&p->m_k, &e, pSk, pNonce);
}

__stack_hungry__
int Signature_IsValid(const Signature* p, const UintBig* pMsg, FlexPoint* pPk)
{
	FlexPoint fpNonce;
	fpNonce.m_Compact = p->m_NoncePub;
	fpNonce.m_Flags = c_FlexPoint_Compact;
	
	FlexPoint_MakeGe(&fpNonce);
	if (!fpNonce.m_Flags)
		return 0;

	secp256k1_scalar k;
	int overflow; // for historical reasons we don't check for overflow, i.e. theoretically there can be an ambiguity, but it makes not much sense for the attacker
	secp256k1_scalar_set_b32(&k, p->m_k.m_pVal, &overflow);


	FlexPoint_MakeGej(pPk);
	if (!pPk->m_Flags)
		return 0; // bad Pubkey

	secp256k1_gej gej;
	secp256k1_fe zDenom;

	MultiMac_WNaf wnaf;
	secp256k1_scalar s;
	MultiMac_Fast gen;

	MultiMac_Context ctx;
	ctx.m_pRes = &gej;
	ctx.m_Secure = 1;
	ctx.m_pGenSecure = Context_get()->m_pGenGJ;
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
		ctx.m_pFastK = &s;
		ctx.m_pWnaf = &wnaf;
		MultiMac_SetCustom_Nnz(&ctx, pPk);

		Signature_GetChallenge(p, pMsg, &s);
	}

	MultiMac_Calculate(&ctx);
	secp256k1_gej_add_ge_var(&gej, &gej, &fpNonce.m_Ge, 0);

	return secp256k1_gej_is_infinity(&gej);
}

//////////////////////////////
// TxKernel
__stack_hungry__
void TxKernel_getID_Ex(const TxKernelUser* pUser, const TxKernelData* pData, UintBig* pMsg, const UintBig* pNestedIDs, uint32_t nNestedIDs)
{
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);

	secp256k1_sha256_write_Num(&sha, pUser->m_Fee);
	secp256k1_sha256_write_Num(&sha, pUser->m_hMin);
	secp256k1_sha256_write_Num(&sha, pUser->m_hMax);

	secp256k1_sha256_write_CompactPoint(&sha, &pData->m_Commitment);
	secp256k1_sha256_write_Num(&sha, 0); // former m_AssetEmission

	uint8_t nFlags = 0; // extended flags, irrelevent for HW wallet
	secp256k1_sha256_write(&sha, &nFlags, sizeof(nFlags));

	for (uint32_t i = 0; i < nNestedIDs; i++)
	{
		secp256k1_sha256_write(&sha, &nFlags, sizeof(nFlags));
		secp256k1_sha256_write(&sha, pNestedIDs[i].m_pVal, sizeof(pNestedIDs[i].m_pVal));
	}

	nFlags = 1; // no more nested kernels
	secp256k1_sha256_write(&sha, &nFlags, sizeof(nFlags));

	secp256k1_sha256_finalize(&sha, pMsg->m_pVal);
}

void TxKernel_getID(const TxKernelUser* pUser, const TxKernelData* pData, UintBig* pMsg)
{
	TxKernel_getID_Ex(pUser, pData, pMsg, 0, 0);
}

__stack_hungry__
int TxKernel_IsValid(const TxKernelUser* pUser, const TxKernelData* pData)
{
	UintBig msg;
	TxKernel_getID(pUser, pData, &msg);

	FlexPoint fp;
	fp.m_Compact = pData->m_Commitment;
	fp.m_Flags = c_FlexPoint_Compact;

	return Signature_IsValid(&pData->m_Signature, &msg, &fp);
}

__stack_hungry__
void TxKernel_SpecialMsg(secp256k1_sha256_t* pSha, Amount fee, Height hMin, Height hMax, uint8_t nType)
{
	// calculate kernel Msg
	secp256k1_sha256_initialize(pSha);
	secp256k1_sha256_write_Num(pSha, fee);
	secp256k1_sha256_write_Num(pSha, hMin);
	secp256k1_sha256_write_Num(pSha, hMax);

	UintBig hv = { 0 };
	secp256k1_sha256_write(pSha, hv.m_pVal, sizeof(hv.m_pVal));
	hv.m_pVal[0] = 1;
	secp256k1_sha256_write(pSha, hv.m_pVal, 1);
	secp256k1_sha256_write_Num(pSha, nType);
	secp256k1_sha256_write(pSha, hv.m_pVal, 1); // nested break
}

//////////////////////////////
// KeyKeeper - pub Kdf export
__stack_hungry__
static void Kdf2Pub(const Kdf* pKdf, KdfPub* pRes)
{
	Context* pCtx = Context_get();

	pRes->m_Secret = pKdf->m_Secret;

	FlexPoint fp;

	MulPoint(&fp, pCtx->m_pGenGJ, &pKdf->m_kCoFactor);
	FlexPoint_MakeCompact(&fp);
	pRes->m_CoFactorG = fp.m_Compact;

	MulPoint(&fp, pCtx->m_pGenGJ + 1, &pKdf->m_kCoFactor);
	FlexPoint_MakeCompact(&fp);
	pRes->m_CoFactorJ = fp.m_Compact;
}

__stack_hungry__
void KeyKeeper_GetPKdf(const KeyKeeper* p, KdfPub* pRes, const uint32_t* pChild)
{
	if (pChild)
	{
		Kdf kdfChild;
		Kdf_getChild(&kdfChild, *pChild, &p->m_MasterKey);
		Kdf2Pub(&kdfChild, pRes);
	}
	else
		Kdf2Pub(&p->m_MasterKey, pRes);
}



//////////////////
// Protocol
#define PROTO_METHOD(name) __stack_hungry__ static int HandleProto_##name(const KeyKeeper* p, Op_##name* pArg, uint32_t nIn, uint32_t nOut)

#pragma pack (push, 1)
#define THE_MACRO_Field(cvt, type, name) type m_##name;
#define THE_MACRO_OpCode(id, name) \
typedef struct { \
	uint8_t m_OpCode; \
	BeamCrypto_ProtoRequest_##name(THE_MACRO_Field) \
} OpIn_##name; \
typedef struct { \
	BeamCrypto_ProtoResponse_##name(THE_MACRO_Field) \
} OpOut_##name; \
typedef union Op_##name { \
	OpIn_##name m_In; \
	OpOut_##name m_Out; \
} Op_##name; \
PROTO_METHOD(name);

BeamCrypto_ProtoMethods(THE_MACRO_OpCode)

#undef THE_MACRO_OpCode
#undef THE_MACRO_Field

#pragma pack (pop)


#define PROTO_METHOD_SIMPLE(name) \
static int HandleProtoSimple_##name(const KeyKeeper* p, OpIn_##name* pIn, uint32_t nIn, OpOut_##name* pOut); \
__stack_hungry__ static int HandleProto_##name(const KeyKeeper* p, Op_##name* pArg, uint32_t nIn, uint32_t nOut) \
{ \
	if (nOut) \
		return c_KeyKeeper_Status_ProtoError; \
	OpOut_##name out; \
	int res = HandleProtoSimple_##name(p, &pArg->m_In, nIn, &out); \
	if (c_KeyKeeper_Status_Ok == res) \
		memcpy(&pArg->m_Out, &out, sizeof(out)); \
	return res; \
} \
static int HandleProtoSimple_##name(const KeyKeeper* p, OpIn_##name* pIn, uint32_t nIn, OpOut_##name* pOut)


#define ProtoH2N(field) WriteInNetworkOrderRaw((uint8_t*) &field, field, sizeof(field))
#define ProtoN2H(field, type) field = (type) ReadInNetworkOrder((uint8_t*) &field, sizeof(field)); static_assert(sizeof(field) == sizeof(type), "")

#define N2H_uint32_t(p) ProtoN2H((*p), uint32_t)
#define H2N_uint32_t(p) ProtoH2N((*p))

#define N2H_uint64_t(p) ProtoN2H((*p), uint64_t)
#define H2N_uint64_t(p) ProtoH2N((*p))

#define N2H_Height(p) ProtoN2H((*p), Height)
#define H2N_Height(p) ProtoH2N((*p))

#define N2H_WalletIdentity(p) ProtoN2H((*p), WalletIdentity)
#define H2N_WalletIdentity(p) ProtoH2N((*p))

void N2H_CoinID(CoinID* p)
{
	ProtoN2H(p->m_Amount, Amount);
	ProtoN2H(p->m_AssetID, AssetID);
	ProtoN2H(p->m_Idx, uint64_t);
	ProtoN2H(p->m_SubIdx, uint32_t);
	ProtoN2H(p->m_Type, uint32_t);
}

void N2H_ShieldedInput(ShieldedInput* p)
{
	ProtoN2H(p->m_Fee, Amount);
	ProtoN2H(p->m_TxoID.m_Amount, Amount);
	ProtoN2H(p->m_TxoID.m_AssetID, AssetID);
	ProtoN2H(p->m_TxoID.m_nViewerIdx, uint32_t);
}

void N2H_TxCommonIn(TxCommonIn* p)
{
	ProtoN2H(p->m_Ins, uint32_t);
	ProtoN2H(p->m_Outs, uint32_t);
	ProtoN2H(p->m_InsShielded, uint32_t);
	ProtoN2H(p->m_Krn.m_Fee, Amount);
	ProtoN2H(p->m_Krn.m_hMin, Height);
	ProtoN2H(p->m_Krn.m_hMax, Height);
}

void N2H_TxMutualIn(TxMutualIn* p)
{
	ProtoN2H(p->m_MyIDKey, WalletIdentity);
}

__stack_hungry__
int KeyKeeper_Invoke(const KeyKeeper* p, uint8_t* pInOut, uint32_t nIn, uint32_t nOut)
{
	if (!nIn)
		return c_KeyKeeper_Status_ProtoError;

	switch (*pInOut)
	{
#define THE_MACRO_CvtIn(cvt, type, name) THE_MACRO_CvtIn_##cvt(type, name)
#define THE_MACRO_CvtIn_0(type, name)
#define THE_MACRO_CvtIn_1(type, name) N2H_##type(&pArg->m_In.m_##name);

#define THE_MACRO_CvtOut(cvt, type, name) THE_MACRO_CvtOut_##cvt(type, name)
#define THE_MACRO_CvtOut_0(type, name)
#define THE_MACRO_CvtOut_1(type, name) H2N_##type(&pArg->m_Out.m_##name);

#define THE_MACRO(id, name) \
	case id: \
	{ \
		if ((nIn < sizeof(OpIn_##name)) || (nOut < sizeof(OpOut_##name))) \
			return c_KeyKeeper_Status_ProtoError; \
 \
		Op_##name* pArg = (Op_##name*) pInOut; \
		BeamCrypto_ProtoRequest_##name(THE_MACRO_CvtIn) \
\
		int nRes = HandleProto_##name(p, pArg, nIn - sizeof(OpIn_##name), nOut - sizeof(OpOut_##name)); \
		if (c_KeyKeeper_Status_Ok == nRes) \
		{ \
			BeamCrypto_ProtoResponse_##name(THE_MACRO_CvtOut) \
		} \
		return nRes; \
	} \
	break; \

		BeamCrypto_ProtoMethods(THE_MACRO)
#undef THE_MACRO

	}

	return c_KeyKeeper_Status_ProtoError;
}

PROTO_METHOD(Version)
{
	if (nIn || nOut)
		return c_KeyKeeper_Status_ProtoError; // size mismatch

	pArg->m_Out.m_Value = BeamCrypto_CurrentProtoVer;
	return c_KeyKeeper_Status_Ok;
}

PROTO_METHOD(GetNumSlots)
{
	if (nIn || nOut)
		return c_KeyKeeper_Status_ProtoError; // size mismatch

	pArg->m_Out.m_Value = KeyKeeper_getNumSlots();
	return c_KeyKeeper_Status_Ok;
}

PROTO_METHOD(GetPKdf)
{
	if (nIn || nOut)
		return c_KeyKeeper_Status_ProtoError; // size mismatch

	uint32_t iChild = (uint32_t) -1;
	KeyKeeper_GetPKdf(p, &pArg->m_Out.m_Value, pArg->m_In.m_Kind ? &iChild : 0);

	return c_KeyKeeper_Status_Ok;
}

PROTO_METHOD(GetImage)
{
	if (nIn || nOut)
		return c_KeyKeeper_Status_ProtoError; // size mismatch

	Kdf kdfC;
	Kdf_getChild(&kdfC, pArg->m_In.m_iChild, &p->m_MasterKey);

	secp256k1_scalar sk;
	Kdf_Derive_SKey(&kdfC, &pArg->m_In.m_hvSrc, &sk);
	SECURE_ERASE_OBJ(kdfC);

	FlexPoint pFlex[2];

	uint8_t bG = pArg->m_In.m_bG; // they would be overwritten by pArg->m_Out
	uint8_t bJ = pArg->m_In.m_bJ;

	if (bG)
		MulG(pFlex, &sk);

	if (bJ)
	{
		MulPoint(pFlex + 1, Context_get()->m_pGenGJ + 1, &sk);

		if (bG)
			FlexPoint_MakeGe_Batch(pFlex, _countof(pFlex));

		FlexPoint_MakeCompact(pFlex + 1);
		pArg->m_Out.m_ptImageJ = pFlex[1].m_Compact;
	}
	else
	{
		if (!bG)
			return c_KeyKeeper_Status_Unspecified;

		memset(&pArg->m_Out.m_ptImageJ, 0, sizeof(pArg->m_Out.m_ptImageJ));
	}

	SECURE_ERASE_OBJ(sk);

	if (bG)
	{
		FlexPoint_MakeCompact(pFlex);
		pArg->m_Out.m_ptImageG = pFlex->m_Compact;
	}
	else
		memset(&pArg->m_Out.m_ptImageG, 0, sizeof(pArg->m_Out.m_ptImageG));

	return c_KeyKeeper_Status_Ok;
}

PROTO_METHOD(CreateOutput)
{
	if (nIn || nOut)
		return c_KeyKeeper_Status_ProtoError; // size mismatch

	RangeProof ctx;
	ctx.m_Cid = pArg->m_In.m_Cid;
	ctx.m_pKdf = &p->m_MasterKey;
	ctx.m_pT_In = pArg->m_In.m_pT;
	ctx.m_pT_Out = pArg->m_In.m_pT; // use same buf (since we changed to in/out buf design). Copy res later

	secp256k1_scalar sBuf;
	ctx.m_pTauX = &sBuf;

	static_assert(sizeof(UintBig) == sizeof(secp256k1_scalar), "");

	if (memis0(pArg->m_In.m_pKExtra->m_pVal, sizeof(pArg->m_In.m_pKExtra)))
		ctx.m_pKExtra = 0;
	else
	{
		// in-place convert, overwrite the original pIn->m_pKExtra
		ctx.m_pKExtra = ctx.m_pTauX;

		for (uint32_t i = 0; i < _countof(pArg->m_In.m_pKExtra); i++)
		{
			memcpy(&sBuf, pArg->m_In.m_pKExtra[i].m_pVal, sizeof(sBuf));
			int overflow;
			secp256k1_scalar_set_b32((secp256k1_scalar*)pArg->m_In.m_pKExtra + i, (uint8_t*) &sBuf, &overflow);
		}
	}

	ctx.m_pAssetGen = IsUintBigZero(&pArg->m_In.m_ptAssetGen.m_X) ? 0 : &pArg->m_In.m_ptAssetGen;

	if (!RangeProof_Calculate(&ctx))
		return c_KeyKeeper_Status_Unspecified;

	// copy into out. To it carefully, since in/out share the same mem

	static_assert(sizeof(pArg->m_Out.m_pT) == sizeof(pArg->m_In.m_pT), "");
	memmove(pArg->m_Out.m_pT, pArg->m_In.m_pT, sizeof(pArg->m_Out.m_pT)); // MUST use memmove!

	secp256k1_scalar_get_b32(pArg->m_Out.m_TauX.m_pVal, &sBuf);

	return c_KeyKeeper_Status_Ok;
}

//////////////////////////////
// KeyKeeper - transaction common. Aggregation
typedef struct
{
	Amount m_Beams;
	Amount m_Assets;

} TxAggr0;

typedef struct
{
	TxAggr0 m_Ins;
	TxAggr0 m_Outs;

	Amount m_TotalFee;
	AssetID m_AssetID;
	secp256k1_scalar m_sk;

} TxAggr;


static int TxAggregate_AddAmount(Amount val, AssetID aid, TxAggr0* pRes, TxAggr* pCommon)
{
	Amount* pVal;
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

__stack_hungry__
static int TxAggregate0(const KeyKeeper* p, CoinID* pCid, uint32_t nCount, TxAggr0* pRes, TxAggr* pCommon, int isOuts)
{
	for (uint32_t i = 0; i < nCount; i++, pCid++)
	{
		N2H_CoinID(pCid);

		uint8_t nScheme;
		uint32_t nSubkey;
		CoinID_getSchemeAndSubkey(pCid, &nScheme, &nSubkey);

		if (nSubkey && isOuts)
			return 0; // HW wallet should not send funds to child subkeys (potentially belonging to miners)

		switch (nScheme)
		{
		case c_CoinID_Scheme_V0:
		case c_CoinID_Scheme_BB21:
			// weak schemes
			if (isOuts)
				return 0; // no reason to create weak outputs

			if (!p->m_AllowWeakInputs)
				return 0;
		}

		if (!TxAggregate_AddAmount(pCid->m_Amount, pCid->m_AssetID, pRes, pCommon))
			return 0;

		secp256k1_scalar sk;
		CoinID_getSk(&p->m_MasterKey, pCid, &sk);

		secp256k1_scalar_add(&pCommon->m_sk, &pCommon->m_sk, &sk);
		SECURE_ERASE_OBJ(sk);
	}

	return 1;
}

__stack_hungry__
static int TxAggregateShIns(const KeyKeeper* p, ShieldedInput* pIns, uint32_t nCount, TxAggr0* pRes, TxAggr* pCommon)
{
	for (uint32_t i = 0; i < nCount; i++, pIns++)
	{
		N2H_ShieldedInput(pIns);

		if (!TxAggregate_AddAmount(pIns->m_TxoID.m_Amount, pIns->m_TxoID.m_AssetID, pRes, pCommon))
			return 0;

		pCommon->m_TotalFee += pIns->m_Fee;
		if (pCommon->m_TotalFee < pIns->m_Fee)
			return 0; // overflow

		secp256k1_scalar sk;
		ShieldedInput_getSk(p, pIns, &sk);

		secp256k1_scalar_add(&pCommon->m_sk, &pCommon->m_sk, &sk);
		SECURE_ERASE_OBJ(sk);
	}

	return 1;
}

static int TxAggregate(const KeyKeeper* p, const TxCommonIn* pTx, TxAggr* pRes, void* pExtra, uint32_t nExtra)
{
	CoinID* pIns = (CoinID*) pExtra;
	CoinID* pOuts = pIns + pTx->m_Ins;
	ShieldedInput* pInsShielded = (ShieldedInput*)(pOuts + pTx->m_Outs);

	if ((uint8_t*) (pInsShielded + pTx->m_InsShielded) != ((uint8_t*) pExtra) + nExtra)
		return 0;

	memset(pRes, 0, sizeof(*pRes));
	pRes->m_TotalFee = pTx->m_Krn.m_Fee;

	if (!TxAggregate0(p, pIns, pTx->m_Ins, &pRes->m_Ins, pRes, 0))
		return 0;

	if (!TxAggregateShIns(p, pInsShielded, pTx->m_InsShielded, &pRes->m_Ins, pRes))
		return 0;

	secp256k1_scalar_negate(&pRes->m_sk, &pRes->m_sk);

	return TxAggregate0(p, pOuts, pTx->m_Outs, &pRes->m_Outs, pRes, 1);
}

static void TxAggrToOffsetEx(TxAggr* pAggr, const secp256k1_scalar* pKrn, UintBig* pOffs)
{
	secp256k1_scalar_add(&pAggr->m_sk, &pAggr->m_sk, pKrn);
	secp256k1_scalar_negate(&pAggr->m_sk, &pAggr->m_sk);
	secp256k1_scalar_get_b32(pOffs->m_pVal, &pAggr->m_sk);
}

static void TxAggrToOffset(TxAggr* pAggr, const secp256k1_scalar* pKrn, TxCommonOut* pTx)
{
	TxAggrToOffsetEx(pAggr, pKrn, &pTx->m_kOffset);
}

static int TxAggregate_SendOrSplit(const KeyKeeper* p, const TxCommonIn* pTx, TxAggr* pRes, void* pExtra, uint32_t nExtra)
{
	if (!TxAggregate(p, pTx, pRes, pExtra, nExtra))
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
// KeyKeeper - Kernel modification
__stack_hungry__
static int KernelUpdateKeysEx(CompactPoint* pCommitment, CompactPoint* pNoncePub, const secp256k1_scalar* pSk, const secp256k1_scalar* pNonce, const TxKernelData* pAdd)
{
	FlexPoint pFp[2];

	MulG(pFp, pSk);
	MulG(pFp + 1, pNonce);

	if (pAdd)
	{
		FlexPoint fp;
		fp.m_Compact = pAdd->m_Commitment;
		fp.m_Flags = c_FlexPoint_Compact;

		FlexPoint_MakeGe(&fp);
		if (!fp.m_Flags)
			return 0;

		secp256k1_gej_add_ge_var(&pFp[0].m_Gej, &pFp[0].m_Gej, &fp.m_Ge, 0);

		fp.m_Compact = pAdd->m_Signature.m_NoncePub;
		fp.m_Flags = c_FlexPoint_Compact;

		FlexPoint_MakeGe(&fp);
		if (!fp.m_Flags)
			return 0;

		secp256k1_gej_add_ge_var(&pFp[1].m_Gej, &pFp[1].m_Gej, &fp.m_Ge, 0);
	}

	FlexPoint_MakeGe_Batch(pFp, _countof(pFp));

	FlexPoint_MakeCompact(pFp);
	*pCommitment = pFp[0].m_Compact;

	FlexPoint_MakeCompact(pFp + 1);
	*pNoncePub = pFp[1].m_Compact;

	return 1;
}

static int KernelUpdateKeys(TxKernelData* pKrn, const secp256k1_scalar* pSk, const secp256k1_scalar* pNonce, const TxKernelData* pAdd)
{
	return KernelUpdateKeysEx(&pKrn->m_Commitment, &pKrn->m_Signature.m_NoncePub, pSk, pNonce, pAdd);
}

//////////////////////////////
// KeyKeeper - SplitTx
PROTO_METHOD_SIMPLE(TxSplit)
{
	TxAggr txAggr;
	if (!TxAggregate_SendOrSplit(p, &pIn->m_Tx, &txAggr, pIn + 1, nIn))
		return c_KeyKeeper_Status_Unspecified;
	if (txAggr.m_Ins.m_Assets)
		return c_KeyKeeper_Status_Unspecified; // not split

	// hash all visible params
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);
	secp256k1_sha256_write_Num(&sha, pIn->m_Tx.m_Krn.m_hMin);
	secp256k1_sha256_write_Num(&sha, pIn->m_Tx.m_Krn.m_hMax);
	secp256k1_sha256_write_Num(&sha, pIn->m_Tx.m_Krn.m_Fee);

	UintBig hv;
	secp256k1_scalar_get_b32(hv.m_pVal, &txAggr.m_sk);
	secp256k1_sha256_write(&sha, hv.m_pVal, sizeof(hv.m_pVal));
	secp256k1_sha256_finalize(&sha, hv.m_pVal);

	// derive keys
	static const char szSalt[] = "hw-wlt-split";
	NonceGenerator ng;
	NonceGenerator_Init(&ng, szSalt, sizeof(szSalt), &hv);

	secp256k1_scalar kKrn, kNonce;
	NonceGenerator_NextScalar(&ng, &kKrn);
	NonceGenerator_NextScalar(&ng, &kNonce);
	SECURE_ERASE_OBJ(ng);

	KernelUpdateKeys(&pOut->m_Tx.m_Krn, &kKrn, &kNonce, 0);

	TxKernel_getID(&pIn->m_Tx.m_Krn, &pOut->m_Tx.m_Krn, &hv);

	int res = KeyKeeper_ConfirmSpend(0, 0, 0, &pIn->m_Tx.m_Krn, &pOut->m_Tx.m_Krn, &hv);
	if (c_KeyKeeper_Status_Ok != res)
		return res;

	Signature_SignPartial(&pOut->m_Tx.m_Krn.m_Signature, &hv, &kKrn, &kNonce);

	TxAggrToOffset(&txAggr, &kKrn, &pOut->m_Tx);

	return c_KeyKeeper_Status_Ok;
}

//////////////////////////////
// KeyKeeper - Receive + Send common stuff
__stack_hungry__
static void GetPaymentConfirmationMsg(UintBig* pRes, const UintBig* pSender, const UintBig* pKernelID, Amount amount, AssetID nAssetID)
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

__stack_hungry__
static void GetWalletIDKey(const KeyKeeper* p, WalletIdentity nKey, secp256k1_scalar* pKey, UintBig* pID)
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

	Kdf_Derive_SKey(&p->m_MasterKey, pID, pKey);
	Sk2Pk(pID, pKey);
}

//////////////////////////////
// KeyKeeper - ReceiveTx
PROTO_METHOD_SIMPLE(TxReceive)
{
	TxAggr txAggr;
	if (!TxAggregate(p, &pIn->m_Tx, &txAggr, pIn + 1, nIn))
		return c_KeyKeeper_Status_Unspecified;

	if (txAggr.m_Ins.m_Beams != txAggr.m_Outs.m_Beams)
	{
		if (txAggr.m_Ins.m_Beams > txAggr.m_Outs.m_Beams)
			return c_KeyKeeper_Status_Unspecified; // not receiving

		if (txAggr.m_Ins.m_Assets != txAggr.m_Outs.m_Assets)
			return c_KeyKeeper_Status_Unspecified; // mixed

		txAggr.m_AssetID = 0;
		txAggr.m_Outs.m_Assets = txAggr.m_Outs.m_Beams - txAggr.m_Ins.m_Beams;
	}
	else
	{
		if (txAggr.m_Ins.m_Assets >= txAggr.m_Outs.m_Assets)
			return c_KeyKeeper_Status_Unspecified; // not receiving

		assert(txAggr.m_AssetID);
		txAggr.m_Outs.m_Assets -= txAggr.m_Ins.m_Assets;
	}

	// Hash *ALL* the parameters, make the context unique
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);

	UintBig hv;
	TxKernel_getID(&pIn->m_Tx.m_Krn, &pIn->m_Krn, &hv); // not a final ID yet

	secp256k1_sha256_write(&sha, hv.m_pVal, sizeof(hv.m_pVal));
	secp256k1_sha256_write_CompactPoint(&sha, &pIn->m_Krn.m_Signature.m_NoncePub);

	uint8_t nFlag = 0; // not nonconventional
	secp256k1_sha256_write(&sha, &nFlag, sizeof(nFlag));
	secp256k1_sha256_write(&sha, pIn->m_Mut.m_Peer.m_pVal, sizeof(pIn->m_Mut.m_Peer.m_pVal));
	secp256k1_sha256_write_Num(&sha, pIn->m_Mut.m_MyIDKey);

	secp256k1_scalar_get_b32(hv.m_pVal, &txAggr.m_sk);
	secp256k1_sha256_write(&sha, hv.m_pVal, sizeof(hv.m_pVal));

	secp256k1_sha256_write_Num(&sha, txAggr.m_Outs.m_Assets); // the value being-received
	secp256k1_sha256_write_Num(&sha, txAggr.m_AssetID);

	secp256k1_sha256_finalize(&sha, hv.m_pVal);

	// derive keys
	static const char szSalt[] = "hw-wlt-rcv";
	NonceGenerator ng;
	NonceGenerator_Init(&ng, szSalt, sizeof(szSalt), &hv);

	secp256k1_scalar kKrn, kNonce;
	NonceGenerator_NextScalar(&ng, &kKrn);
	NonceGenerator_NextScalar(&ng, &kNonce);
	SECURE_ERASE_OBJ(ng);

	if (!KernelUpdateKeys(&pOut->m_Tx.m_Krn, &kKrn, &kNonce, &pIn->m_Krn))
		return c_KeyKeeper_Status_Unspecified;

	TxKernel_getID(&pIn->m_Tx.m_Krn, &pOut->m_Tx.m_Krn, &hv); // final ID
	Signature_SignPartial(&pOut->m_Tx.m_Krn.m_Signature, &hv, &kKrn, &kNonce);

	TxAggrToOffset(&txAggr, &kKrn, &pOut->m_Tx);

	if (pIn->m_Mut.m_MyIDKey)
	{
		// sign
		UintBig hvID;
		GetWalletIDKey(p, pIn->m_Mut.m_MyIDKey, &kKrn, &hvID);
		GetPaymentConfirmationMsg(&hvID, &pIn->m_Mut.m_Peer, &hv, txAggr.m_Outs.m_Assets, txAggr.m_AssetID);
		Signature_Sign(&pOut->m_PaymentProof, &hvID, &kKrn);
	}

	return c_KeyKeeper_Status_Ok;
}

//////////////////////////////
// KeyKeeper - SendTx
int HandleTxSend(const KeyKeeper* p, OpIn_TxSend2* pIn, void* pInExtra, uint32_t nInExtra, OpOut_TxSend1* pOut1, OpOut_TxSend2* pOut2)
{
	TxAggr txAggr;
	if (!TxAggregate_SendOrSplit(p, &pIn->m_Tx, &txAggr, pInExtra, nInExtra))
		return c_KeyKeeper_Status_Unspecified;
	if (!txAggr.m_Ins.m_Assets)
		return c_KeyKeeper_Status_Unspecified; // not sending (no net transferred value)

	if (IsUintBigZero(&pIn->m_Mut.m_Peer))
		return c_KeyKeeper_Status_UserAbort; // conventional transfers must always be signed

	secp256k1_scalar kKrn, kNonce;
	UintBig hvMyID, hv;
	GetWalletIDKey(p, pIn->m_Mut.m_MyIDKey, &kNonce, &hvMyID);

	if (pIn->m_iSlot >= KeyKeeper_getNumSlots())
		return c_KeyKeeper_Status_Unspecified;

	KeyKeeper_ReadSlot(pIn->m_iSlot, &hv);
	Kdf_Derive_SKey(&p->m_MasterKey, &hv, &kNonce);

	// during negotiation kernel height and commitment are adjusted. We should only commit to the Fee
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);
	secp256k1_sha256_write_Num(&sha, pIn->m_Tx.m_Krn.m_Fee);
	secp256k1_sha256_write(&sha, pIn->m_Mut.m_Peer.m_pVal, sizeof(pIn->m_Mut.m_Peer.m_pVal));
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
	NonceGenerator ng;
	NonceGenerator_Init(&ng, szSalt, sizeof(szSalt), &hv);
	NonceGenerator_NextScalar(&ng, &kKrn);
	SECURE_ERASE_OBJ(ng);

	// derive tx token
	secp256k1_sha256_initialize(&sha);
	HASH_WRITE_STR(sha, "tx.token");

	secp256k1_scalar_get_b32(hv.m_pVal, &kKrn);
	secp256k1_sha256_write(&sha, hv.m_pVal, sizeof(hv.m_pVal));
	secp256k1_sha256_finalize(&sha, hv.m_pVal);

	if (IsUintBigZero(&hv))
		hv.m_pVal[_countof(hv.m_pVal) - 1] = 1;

	if (pOut1)
	{
		int res = KeyKeeper_ConfirmSpend(txAggr.m_Ins.m_Assets, txAggr.m_AssetID, &pIn->m_Mut.m_Peer, &pIn->m_Tx.m_Krn, 0, 0);
		if (c_KeyKeeper_Status_Ok != res)
			return res;

		pOut1->m_UserAgreement = hv;

		KernelUpdateKeysEx(&pOut1->m_HalfKrn.m_Commitment, &pOut1->m_HalfKrn.m_NoncePub, &kKrn, &kNonce, 0);

		return c_KeyKeeper_Status_Ok;
	}

	assert(pOut2);

	if (memcmp(pIn->m_UserAgreement.m_pVal, hv.m_pVal, sizeof(hv.m_pVal)))
		return c_KeyKeeper_Status_Unspecified; // incorrect user agreement token

	TxKernelData krn;
	krn.m_Commitment = pIn->m_HalfKrn.m_Commitment;
	krn.m_Signature.m_NoncePub = pIn->m_HalfKrn.m_NoncePub;

	TxKernel_getID(&pIn->m_Tx.m_Krn, &krn, &hv);

	// verify payment confirmation signature
	GetPaymentConfirmationMsg(&hvMyID, &hvMyID, &hv, txAggr.m_Ins.m_Assets, txAggr.m_AssetID);

	FlexPoint fp;
	fp.m_Compact.m_X = pIn->m_Mut.m_Peer;
	fp.m_Compact.m_Y = 0;
	fp.m_Flags = c_FlexPoint_Compact;

	if (!Signature_IsValid(&pIn->m_PaymentProof, &hvMyID, &fp))
		return c_KeyKeeper_Status_Unspecified;

	// 2nd user confirmation request. Now the kernel is complete, its ID is calculated
	int res = KeyKeeper_ConfirmSpend(txAggr.m_Ins.m_Assets, txAggr.m_AssetID, &pIn->m_Mut.m_Peer, &pIn->m_Tx.m_Krn, &krn, &hvMyID);
	if (c_KeyKeeper_Status_Ok != res)
		return res;

	// Regenerate the slot (BEFORE signing), and sign
	KeyKeeper_RegenerateSlot(pIn->m_iSlot);

	Signature_SignPartial(&krn.m_Signature, &hv, &kKrn, &kNonce);

	pOut2->m_kSig = krn.m_Signature.m_k;
	TxAggrToOffsetEx(&txAggr, &kKrn, &pOut2->m_kOffset);

	return c_KeyKeeper_Status_Ok;
}

PROTO_METHOD_SIMPLE(TxSend1)
{
	return HandleTxSend(p, (OpIn_TxSend2*) pIn, pIn + 1, nIn, pOut, 0);
}

PROTO_METHOD_SIMPLE(TxSend2)
{
	return HandleTxSend(p, pIn, pIn + 1, nIn, 0, pOut);
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
	Kdf m_Gen;
	Kdf m_Ser;

} ShieldedViewer;

__stack_hungry__
static void ShieldedViewerInit(ShieldedViewer* pRes, uint32_t iViewer, const KeyKeeper* p)
{
	// Shielded viewer
	UintBig hv;
	secp256k1_sha256_t sha;
	secp256k1_scalar sk;

	ShieldedHashTxt(&sha);
	HASH_WRITE_STR(sha, "Own.Gen");
	secp256k1_sha256_write_Num(&sha, iViewer);
	secp256k1_sha256_finalize(&sha, hv.m_pVal);

	Kdf_Derive_PKey(&p->m_MasterKey, &hv, &sk);
	secp256k1_scalar_get_b32(hv.m_pVal, &sk);

	Kdf_Init(&pRes->m_Gen, &hv);

	ShieldedHashTxt(&sha);
	HASH_WRITE_STR(sha, "Own.Ser");
	secp256k1_sha256_write_Num(&sha, iViewer);
	secp256k1_sha256_finalize(&sha, hv.m_pVal);

	Kdf_Derive_PKey(&p->m_MasterKey, &hv, &sk);
	secp256k1_scalar_get_b32(hv.m_pVal, &sk);

	Kdf_Derive_PKey(&p->m_MasterKey, &hv, &sk);
	secp256k1_scalar_get_b32(hv.m_pVal, &sk);

	Kdf_Init(&pRes->m_Ser, &hv);
	secp256k1_scalar_mul(&pRes->m_Ser.m_kCoFactor, &pRes->m_Ser.m_kCoFactor, &sk);
}

static void MulGJ(FlexPoint* pFlex, const secp256k1_scalar* pK)
{
	MultiMac_Context ctx;
	ctx.m_pRes = &pFlex->m_Gej;
	ctx.m_pZDenom = 0;
	ctx.m_Fast = 0;
	ctx.m_Secure = 2;
	ctx.m_pGenSecure = Context_get()->m_pGenGJ;
	ctx.m_pSecureK = pK;

	MultiMac_Calculate(&ctx);
	pFlex->m_Flags = c_FlexPoint_Gej;
}

__stack_hungry__
static void Ticket_Hash(UintBig* pRes, const ShieldedVoucher* pVoucher)
{
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);
	HASH_WRITE_STR(sha, "Out-S");
	secp256k1_sha256_write_CompactPoint(&sha, &pVoucher->m_SerialPub);
	secp256k1_sha256_finalize(&sha, pRes->m_pVal);
}

__stack_hungry__
static void Voucher_Hash(UintBig* pRes, const ShieldedVoucher* pVoucher)
{
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);
	HASH_WRITE_STR(sha, "voucher.1");
	secp256k1_sha256_write_CompactPoint(&sha, &pVoucher->m_SerialPub);
	secp256k1_sha256_write_CompactPoint(&sha, &pVoucher->m_NoncePub);
	secp256k1_sha256_write(&sha, pVoucher->m_SharedSecret.m_pVal, sizeof(pVoucher->m_SharedSecret.m_pVal));
	secp256k1_sha256_finalize(&sha, pRes->m_pVal);
}

__stack_hungry__
static void ShieldedGetSpendKey(const ShieldedViewer* pViewer, const secp256k1_scalar* pkG, uint8_t nIsGenByViewer, UintBig* pPreimage, secp256k1_scalar* pSk)
{
	secp256k1_sha256_t sha;
	ShieldedHashTxt(&sha);
	HASH_WRITE_STR(sha, "kG-k");
	secp256k1_scalar_get_b32(pPreimage->m_pVal, pkG);
	secp256k1_sha256_write(&sha, pPreimage->m_pVal, sizeof(pPreimage->m_pVal));
	secp256k1_sha256_finalize(&sha, pPreimage->m_pVal);

	if (nIsGenByViewer)
		Kdf_Derive_SKey(&pViewer->m_Gen, pPreimage, pSk);
	else
		Kdf_Derive_PKey(&pViewer->m_Gen, pPreimage, pSk);

	ShieldedHashTxt(&sha);
	HASH_WRITE_STR(sha, "k-pI");
	secp256k1_scalar_get_b32(pPreimage->m_pVal, pSk);
	secp256k1_sha256_write(&sha, pPreimage->m_pVal, sizeof(pPreimage->m_pVal));
	secp256k1_sha256_finalize(&sha, pPreimage->m_pVal); // SerialPreimage

	Kdf_Derive_SKey(&pViewer->m_Ser, pPreimage, pSk); // spend sk
}

__stack_hungry__
static void CreateVoucherInternal(ShieldedVoucher* pRes, const UintBig* pNonce, const ShieldedViewer* pViewer)
{
	secp256k1_scalar pK[2], pN[2], sk;
	UintBig hv;
	Oracle oracle;

	// nonce -> kG
	ShieldedHashTxt(&oracle.m_sha);
	HASH_WRITE_STR(oracle.m_sha, "kG");
	secp256k1_sha256_write(&oracle.m_sha, pNonce->m_pVal, sizeof(pNonce->m_pVal));
	secp256k1_sha256_finalize(&oracle.m_sha, hv.m_pVal);
	Kdf_Derive_PKey(&pViewer->m_Gen, &hv, pK);

	// kG -> serial preimage and spend sk
	ShieldedGetSpendKey(pViewer, pK, 1, &hv, &sk);

	FlexPoint pt;
	MulG(&pt, &sk); // spend pk

	Oracle_Init(&oracle);
	HASH_WRITE_STR(oracle.m_sha, "L.Spend");
	secp256k1_sha256_write_Point(&oracle.m_sha, &pt);
	Oracle_NextScalar(&oracle, pK + 1); // serial

	MulGJ(&pt, pK);
	FlexPoint_MakeCompact(&pt);
	pRes->m_SerialPub = pt.m_Compact; // kG*G + serial*J

	// DH
	ShieldedHashTxt(&oracle.m_sha);
	HASH_WRITE_STR(oracle.m_sha, "DH");
	secp256k1_sha256_write_CompactPoint(&oracle.m_sha, &pRes->m_SerialPub);
	secp256k1_sha256_finalize(&oracle.m_sha, hv.m_pVal);
	Kdf_Derive_SKey(&pViewer->m_Gen, &hv, &sk); // DH multiplier

	secp256k1_scalar_mul(pN, pK, &sk);
	secp256k1_scalar_mul(pN + 1, pK + 1, &sk);
	MulGJ(&pt, pN); // shared point

	ShieldedHashTxt(&oracle.m_sha);
	HASH_WRITE_STR(oracle.m_sha, "sp-sec");
	secp256k1_sha256_write_Point(&oracle.m_sha, &pt);
	secp256k1_sha256_finalize(&oracle.m_sha, pRes->m_SharedSecret.m_pVal); // Shared secret

	// nonces
	ShieldedHashTxt(&oracle.m_sha);
	HASH_WRITE_STR(oracle.m_sha, "nG");
	secp256k1_sha256_write(&oracle.m_sha, pRes->m_SharedSecret.m_pVal, sizeof(pRes->m_SharedSecret.m_pVal));
	secp256k1_sha256_finalize(&oracle.m_sha, hv.m_pVal);
	Kdf_Derive_PKey(&pViewer->m_Gen, &hv, pN);

	ShieldedHashTxt(&oracle.m_sha);
	HASH_WRITE_STR(oracle.m_sha, "nJ");
	secp256k1_sha256_write(&oracle.m_sha, pRes->m_SharedSecret.m_pVal, sizeof(pRes->m_SharedSecret.m_pVal));
	secp256k1_sha256_finalize(&oracle.m_sha, hv.m_pVal);
	Kdf_Derive_PKey(&pViewer->m_Gen, &hv, pN + 1);

	MulGJ(&pt, pN);
	FlexPoint_MakeCompact(&pt);
	pRes->m_NoncePub = pt.m_Compact; // nG*G + nJ*J

	// sign it
	Ticket_Hash(&hv, pRes);
	Signature_GetChallengeEx(&pRes->m_NoncePub, &hv, &sk);
	Signature_SignPartialEx(pRes->m_pK, &sk, pK, pN);
	Signature_SignPartialEx(pRes->m_pK + 1, &sk, pK + 1, pN + 1);
}

PROTO_METHOD(CreateShieldedVouchers)
{
	if (nIn)
		return c_KeyKeeper_Status_ProtoError;

	OpIn_CreateShieldedVouchers inp = pArg->m_In;
	if (!inp.m_Count)
		return c_KeyKeeper_Status_Ok;

	if (nOut != sizeof(ShieldedVoucher) * inp.m_Count)
		return c_KeyKeeper_Status_ProtoError;

	ShieldedViewer viewer;
	ShieldedViewerInit(&viewer, 0, p);

	// key to sign the voucher(s)
	UintBig hv;
	secp256k1_scalar skSign;
	GetWalletIDKey(p, inp.m_nMyIDKey, &skSign, &hv);

	ShieldedVoucher* pRes = (ShieldedVoucher*)(&pArg->m_Out + 1);

	for (uint32_t i = 0; ; pRes++)
	{
		CreateVoucherInternal(pRes, &inp.m_Nonce0, &viewer);

		Voucher_Hash(&hv, pRes);
		Signature_Sign(&pRes->m_Signature, &hv, &skSign);

		if (++i == inp.m_Count)
			break;

		// regenerate nonce
		Oracle oracle;
		secp256k1_sha256_initialize(&oracle.m_sha);
		HASH_WRITE_STR(oracle.m_sha, "sh.v.n");
		secp256k1_sha256_write(&oracle.m_sha, inp.m_Nonce0.m_pVal, sizeof(inp.m_Nonce0.m_pVal));
		secp256k1_sha256_finalize(&oracle.m_sha, inp.m_Nonce0.m_pVal);
	}

	pArg->m_Out.m_Count = inp.m_Count;
	return c_KeyKeeper_Status_Ok;
}

//////////////////////////////
// KeyKeeper - CreateShieldedInput
PROTO_METHOD_SIMPLE(CreateShieldedInput)
{
	CompactPoint* pG = (CompactPoint*)(pIn + 1);
	if (nIn != sizeof(*pG) * pIn->m_Sigma_M)
		return c_KeyKeeper_Status_ProtoError;

	Oracle oracle;
	secp256k1_scalar skOutp, skSpend, pN[3];
	FlexPoint comm;
	UintBig hv, hvSigGen;

	ShieldedViewer viewer;
	ShieldedViewerInit(&viewer, pIn->m_Inp.m_TxoID.m_nViewerIdx, p);

	// calculate kernel Msg
	TxKernel_SpecialMsg(&oracle.m_sha, pIn->m_Inp.m_Fee, pIn->m_hMin, pIn->m_hMax, 4);
	secp256k1_sha256_write_Num(&oracle.m_sha, pIn->m_WindowEnd);
	secp256k1_sha256_finalize(&oracle.m_sha, hv.m_pVal);

	// init oracle
	secp256k1_sha256_initialize(&oracle.m_sha);
	secp256k1_sha256_write(&oracle.m_sha, hv.m_pVal, sizeof(hv.m_pVal));

	// starting from HF3 commitmens to shielded state and asset are mandatory
	secp256k1_sha256_write(&oracle.m_sha, pIn->m_ShieldedState.m_pVal, sizeof(pIn->m_ShieldedState.m_pVal));
	secp256k1_sha256_write_CompactPointOptional2(&oracle.m_sha, &pIn->m_ptAssetGen, !IsUintBigZero(&pIn->m_ptAssetGen.m_X));

	secp256k1_sha256_write_Num(&oracle.m_sha, pIn->m_Sigma_n);
	secp256k1_sha256_write_Num(&oracle.m_sha, pIn->m_Sigma_M);

	// output commitment
	ShieldedInput_getSk(p, &pIn->m_Inp, &skOutp); // TODO: use isolated child!
	CoinID_getCommRaw(&skOutp, pIn->m_Inp.m_TxoID.m_Amount, pIn->m_Inp.m_TxoID.m_AssetID, &comm);
	secp256k1_sha256_write_Point(&oracle.m_sha, &comm);

	// spend sk/pk
	int overflow;
	secp256k1_scalar_set_b32(&skSpend, pIn->m_Inp.m_TxoID.m_kSerG.m_pVal, &overflow);
	if (overflow)
		return c_KeyKeeper_Status_Unspecified;

	ShieldedGetSpendKey(&viewer, &skSpend, pIn->m_Inp.m_TxoID.m_IsCreatedByViewer, &hv, &skSpend);
	MulG(&comm, &skSpend);
	secp256k1_sha256_write_Point(&oracle.m_sha, &comm);

	Oracle_NextHash(&oracle, &hvSigGen);

	// Sigma::Part1
	for (uint32_t i = 0; i < _countof(pIn->m_pABCD); i++)
		secp256k1_sha256_write_CompactPoint(&oracle.m_sha, pIn->m_pABCD + i);

	{
		// hash all the visible to-date params
		union {
			secp256k1_sha256_t sha;
			NonceGenerator ng;
		} u;

		u.sha = oracle.m_sha; // copy
		for (uint32_t i = 0; i < pIn->m_Sigma_M; i++)
			secp256k1_sha256_write_CompactPoint(&u.sha, pG + i);

		secp256k1_scalar_get_b32(hv.m_pVal, &skOutp); // secret (invisible for the host)
		secp256k1_sha256_write(&u.sha, hv.m_pVal, sizeof(hv.m_pVal));

		secp256k1_sha256_write(&u.sha, pIn->m_AssetSk.m_pVal, sizeof(pIn->m_AssetSk.m_pVal));
		secp256k1_sha256_write(&u.sha, pIn->m_OutpSk.m_pVal, sizeof(pIn->m_OutpSk.m_pVal));
		secp256k1_sha256_finalize(&u.sha, hv.m_pVal);

		// use current secret hv to seed our nonce generator
		static const char szSalt[] = "lelantus.1";
		NonceGenerator_Init(&u.ng, szSalt, sizeof(szSalt), &hv);

		for (uint32_t i = 0; i < _countof(pN); i++)
			NonceGenerator_NextScalar(&u.ng, pN + i);

		SECURE_ERASE_OBJ(u);
	}


	{
		// SigGen
		secp256k1_scalar sAmount, s1, e;

		CoinID_getCommRawEx(pN, pN + 1, pIn->m_Inp.m_TxoID.m_AssetID, &comm);
		FlexPoint_MakeCompact(&comm);
		pOut->m_NoncePub = comm.m_Compact;

		Oracle o2;
		Oracle_Init(&o2);
		secp256k1_sha256_write_CompactPoint(&o2.m_sha, &comm.m_Compact);
		secp256k1_sha256_write(&o2.m_sha, hvSigGen.m_pVal, sizeof(hvSigGen.m_pVal));

		secp256k1_scalar_set_b32(&e, pIn->m_AssetSk.m_pVal, &overflow); // the 'mix' term

		// nG += nH * assetSk
		secp256k1_scalar_mul(&s1, &e, pN + 1);
		secp256k1_scalar_add(pN, pN, &s1);

		// skOutp` = skOutp + amount * assetSk
		secp256k1_scalar_set_u64(&sAmount, pIn->m_Inp.m_TxoID.m_Amount);
		secp256k1_scalar_mul(&s1, &e, &sAmount);
		secp256k1_scalar_add(&s1, &s1, &skOutp);

		// 1st challenge
		Oracle_NextScalar(&o2, &e);

		secp256k1_scalar_mul(&s1, &s1, &e);
		secp256k1_scalar_add(pN, pN, &s1); // nG += skOutp` * e

		secp256k1_scalar_mul(&s1, &sAmount, &e);
		secp256k1_scalar_add(pN + 1, pN + 1, &s1); // nH += amount * e

		// 2nd challenge
		Oracle_NextScalar(&o2, &e);
		secp256k1_scalar_mul(&s1, &skSpend, &e);
		secp256k1_scalar_add(pN, pN, &s1); // nG += skSpend * e

		static_assert(_countof(pN) >= _countof(pOut->m_pSig), "");
		for (uint32_t i = 0; i < _countof(pOut->m_pSig); i++)
		{
			secp256k1_scalar_negate(pN + i, pN + i);
			secp256k1_scalar_get_b32(pOut->m_pSig[i].m_pVal, pN + i);
		}
	}

	comm.m_Compact = pG[0];
	comm.m_Flags = c_FlexPoint_Compact;

	FlexPoint_MakeGe(&comm);
	if (!comm.m_Flags)
		return c_KeyKeeper_Status_Unspecified; // import failed

	{
		FlexPoint comm2;
		MulG(&comm2, pN + 2);
		secp256k1_gej_add_ge_var(&comm2.m_Gej, &comm2.m_Gej, &comm.m_Ge, 0);

		FlexPoint_MakeCompact(&comm2);
		pOut->m_G0 = comm2.m_Compact;
		secp256k1_sha256_write_CompactPoint(&oracle.m_sha, &pOut->m_G0);
	}

	for (uint32_t i = 1; i < pIn->m_Sigma_M; i++)
		secp256k1_sha256_write_CompactPoint(&oracle.m_sha, pG + i);

	secp256k1_scalar e, xPwr;
	Oracle_NextScalar(&oracle, &e);

	// calculate zR
	xPwr = e;
	for (uint32_t i = 1; i < pIn->m_Sigma_M; i++)
		secp256k1_scalar_mul(&xPwr, &xPwr, &e);

	secp256k1_scalar_negate(&skOutp, &skOutp);
	secp256k1_scalar_set_b32(pN, pIn->m_OutpSk.m_pVal, &overflow);
	secp256k1_scalar_add(&skOutp, &skOutp, pN); // skOld - skNew
	secp256k1_scalar_mul(&skOutp, &skOutp, &xPwr);

	secp256k1_scalar_negate(pN + 2, pN + 2);
	secp256k1_scalar_add(pN + 2, pN + 2, &skOutp); // (skOld - skNew) * xPwr - tau

	secp256k1_scalar_get_b32(pOut->m_zR.m_pVal, pN + 2);


	SECURE_ERASE_OBJ(skSpend);
	SECURE_ERASE_OBJ(skOutp);
	SECURE_ERASE_OBJ(pN);

	return c_KeyKeeper_Status_Ok;
}


//////////////////////////////
// KeyKeeper - SendShieldedTx
static uint8_t Msg2Scalar(secp256k1_scalar* p, const UintBig* pMsg)
{
	int overflow;
	secp256k1_scalar_set_b32(p, pMsg->m_pVal, &overflow);
	return !!overflow;
}

__stack_hungry__
int VerifyShieldedOutputParams(const KeyKeeper* p, const OpIn_TxSendShielded* pSh, Amount amount, AssetID aid, secp256k1_scalar* pSk, UintBig* pKrnID)
{
	// check the voucher
	UintBig hv;
	Voucher_Hash(&hv, &pSh->m_Voucher);

	FlexPoint comm;
	comm.m_Compact.m_X = pSh->m_Mut.m_Peer;
	comm.m_Compact.m_Y = 0;
	comm.m_Flags = c_FlexPoint_Compact;

	if (!Signature_IsValid(&pSh->m_Voucher.m_Signature, &hv, &comm))
		return 0;
	// skip the voucher's ticket verification, don't care if it's valid, as it was already signed by the receiver.

	if (pSh->m_Mut.m_MyIDKey)
	{
		GetWalletIDKey(p, pSh->m_Mut.m_MyIDKey, pSk, &hv);
		if (memcmp(hv.m_pVal, pSh->m_Mut.m_Peer.m_pVal, sizeof(hv.m_pVal)))
			return 0;
	}

	// The host expects a TxKernelStd, which contains TxKernelShieldedOutput as a nested kernel, which contains the ShieldedTxo
	// This ShieldedTxo must use the ticket from the voucher, have appropriate commitment + rangeproof, which MUST be generated w.r.t. SharedSecret from the voucher.
	// The receiver *MUST* be able to decode all the parameters w.r.t. the protocol.
	//
	// So, here we ensure that the protocol is obeyed, i.e. the receiver will be able to decode all the parameters from the rangeproof
	//
	// Important: the TxKernelShieldedOutput kernel ID has the whole rangeproof serialized, means it can't be tampered with after we sign the outer kernel

	secp256k1_scalar pExtra[2];

	uint8_t nFlagsPacked = Msg2Scalar(pExtra, &pSh->m_User.m_Sender);

	{
		static const char szSalt[] = "kG-O";
		NonceGenerator ng; // not really secret
		NonceGenerator_Init(&ng, szSalt, sizeof(szSalt), &pSh->m_Voucher.m_SharedSecret);
		NonceGenerator_NextScalar(&ng, pSk);
	}

	secp256k1_scalar_add(pSk, pSk, pExtra); // output blinding factor
	CoinID_getCommRaw(pSk, amount, aid, &comm); // output commitment

	nFlagsPacked |= (Msg2Scalar(pExtra, &pSh->m_User.m_pMessage[0]) << 1);
	nFlagsPacked |= (Msg2Scalar(pExtra + 1, &pSh->m_User.m_pMessage[1]) << 2);

	// We have the commitment, and params that are supposed to be packed in the rangeproof.
	// Recover the parameters, make sure they match

	Oracle oracle;
	TxKernel_SpecialMsg(&oracle.m_sha, 0, 0, -1, 3);
	secp256k1_sha256_finalize(&oracle.m_sha, pKrnID->m_pVal); // krn.Msg

	secp256k1_sha256_initialize(&oracle.m_sha);
	secp256k1_sha256_write(&oracle.m_sha, pKrnID->m_pVal, sizeof(pKrnID->m_pVal)); // oracle << krn.Msg
	secp256k1_sha256_write_CompactPoint(&oracle.m_sha, &pSh->m_Voucher.m_SerialPub);
	secp256k1_sha256_write_CompactPoint(&oracle.m_sha, &pSh->m_Voucher.m_NoncePub);
	secp256k1_sha256_write_Point(&oracle.m_sha, &comm);
	secp256k1_sha256_write_CompactPointOptional2(&oracle.m_sha, &pSh->m_ptAssetGen, !IsUintBigZero(&pSh->m_ptAssetGen.m_X)); // starting from HF3 it's mandatory

	{
		Oracle o2 = oracle;
		HASH_WRITE_STR(o2.m_sha, "bp-s");
		secp256k1_sha256_write(&o2.m_sha, pSh->m_Voucher.m_SharedSecret.m_pVal, sizeof(pSh->m_Voucher.m_SharedSecret.m_pVal));
		secp256k1_sha256_finalize(&o2.m_sha, hv.m_pVal); // seed
	}

	{
#pragma pack (push, 1)
		typedef struct
		{
			uint8_t m_pAssetID[sizeof(AssetID)];
			uint8_t m_Flags;
		} ShieldedTxo_RangeProof_Packed;
#pragma pack (pop)

		secp256k1_scalar skRecovered;
		secp256k1_scalar pExtraRecovered[2];
		ShieldedTxo_RangeProof_Packed packed;

		RangeProof_Recovery_Context ctx;
		ctx.m_pExtra = 0;
		ctx.m_SeedGen = hv;
		ctx.m_pSeedSk = &ctx.m_SeedGen;
		ctx.m_pSk = &skRecovered;
		ctx.m_pUser = &packed;
		ctx.m_nUser = sizeof(packed);
		ctx.m_pExtra = pExtraRecovered;

		if (!RangeProof_Recover(&pSh->m_RangeProof, &oracle, &ctx))
			return 0;

		if (memcmp(pExtra, pExtraRecovered, sizeof(pExtra)) ||
			(packed.m_Flags != nFlagsPacked) ||
			(ctx.m_Amount != amount) ||
			(ReadInNetworkOrder(packed.m_pAssetID, sizeof(packed.m_pAssetID)) != aid))
			return 0;

		if (aid || pSh->m_HideAssetAlways)
		{
			static const char szSalt[] = "skG-O";
			NonceGenerator ng; // not really secret
			NonceGenerator_Init(&ng, szSalt, sizeof(szSalt), &pSh->m_Voucher.m_SharedSecret);
			NonceGenerator_NextScalar(&ng, pExtraRecovered);

			secp256k1_scalar_set_u64(pExtraRecovered + 1, amount);
			secp256k1_scalar_mul(pExtraRecovered, pExtraRecovered, pExtraRecovered + 1);
			secp256k1_scalar_add(&skRecovered, &skRecovered, pExtraRecovered);
		}

		if (memcmp(pSk, &skRecovered, sizeof(skRecovered)))
			return 0;
	}

	// all match! Calculate the resulting kernelID
	secp256k1_sha256_initialize(&oracle.m_sha);
	secp256k1_sha256_write(&oracle.m_sha, pKrnID->m_pVal, sizeof(pKrnID->m_pVal));
	secp256k1_sha256_write(&oracle.m_sha, (uint8_t*) &pSh->m_RangeProof, sizeof(pSh->m_RangeProof));
	secp256k1_sha256_finalize(&oracle.m_sha, pKrnID->m_pVal);

	return 1;
}

PROTO_METHOD_SIMPLE(TxSendShielded)
{
	TxAggr txAggr;
	if (!TxAggregate_SendOrSplit(p, &pIn->m_Tx, &txAggr, pIn + 1, nIn))
		return c_KeyKeeper_Status_Unspecified;

	if (IsUintBigZero(&pIn->m_Mut.m_Peer))
		return c_KeyKeeper_Status_UserAbort; // conventional transfers must always be signed

	UintBig hvKrn1, hv;
	secp256k1_scalar skKrn1, skKrnOuter, kNonce;
	if (!VerifyShieldedOutputParams(p, pIn, txAggr.m_Ins.m_Assets, txAggr.m_AssetID, &skKrn1, &hvKrn1))
		return c_KeyKeeper_Status_Unspecified;

	// select blinding factor for the outer kernel.
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);
	secp256k1_sha256_write(&sha, hvKrn1.m_pVal, sizeof(hvKrn1.m_pVal));
	secp256k1_sha256_write_Num(&sha, pIn->m_Tx.m_Krn.m_hMin);
	secp256k1_sha256_write_Num(&sha, pIn->m_Tx.m_Krn.m_hMax);
	secp256k1_sha256_write_Num(&sha, pIn->m_Tx.m_Krn.m_Fee);
	secp256k1_scalar_get_b32(hv.m_pVal, &txAggr.m_sk);
	secp256k1_sha256_write(&sha, hv.m_pVal, sizeof(hv.m_pVal));
	secp256k1_sha256_finalize(&sha, hv.m_pVal);

	// derive keys
	static const char szSalt[] = "hw-wlt-snd-sh";
	NonceGenerator ng;
	NonceGenerator_Init(&ng, szSalt, sizeof(szSalt), &hv);
	NonceGenerator_NextScalar(&ng, &skKrnOuter);
	NonceGenerator_NextScalar(&ng, &kNonce);
	SECURE_ERASE_OBJ(ng);

	KernelUpdateKeys(&pOut->m_Tx.m_Krn, &skKrnOuter, &kNonce, 0);
	TxKernel_getID_Ex(&pIn->m_Tx.m_Krn, &pOut->m_Tx.m_Krn, &hv, &hvKrn1, 1);

	// all set
	int res = pIn->m_Mut.m_MyIDKey ?
		KeyKeeper_ConfirmSpend(0, 0, 0, &pIn->m_Tx.m_Krn, &pOut->m_Tx.m_Krn, &hv) :
		KeyKeeper_ConfirmSpend(txAggr.m_Ins.m_Assets, txAggr.m_AssetID, &pIn->m_Mut.m_Peer, &pIn->m_Tx.m_Krn, &pOut->m_Tx.m_Krn, &hv);

	if (c_KeyKeeper_Status_Ok != res)
		return res;

	Signature_SignPartial(&pOut->m_Tx.m_Krn.m_Signature, &hv, &skKrnOuter, &kNonce);

	secp256k1_scalar_add(&skKrnOuter, &skKrnOuter, &skKrn1);
	TxAggrToOffset(&txAggr, &skKrnOuter, &pOut->m_Tx);

	return c_KeyKeeper_Status_Ok;
}
