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
#	define __attribute__(x) __declspec x
#endif

#ifndef UNUSED
#	define UNUSED(x) (x)
#endif // UNUSED

#ifdef BeamCrypto_ScarceStack
#	define __stack_hungry__ __attribute__((noinline))
#else // BeamCrypto_ScarceStack
#	define __stack_hungry__
#endif // BeamCrypto_ScarceStack


#include "secp256k1/src/group_impl.h"
#include "secp256k1/src/scalar_impl.h"
#include "secp256k1/src/field_impl.h"
#include "secp256k1/src/hash_impl.h"
#include "secp256k1/src/int128_impl.h"

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic pop
#else
#	pragma warning (default: 4706 4701)
#	pragma warning (pop)
#endif

#define SECURE_ERASE_OBJ(x) SecureEraseMem(&(x), sizeof(x))
#define ZERO_OBJ(x) memset(&(x), 0, sizeof(x))

#if defined(SECP256K1_WIDEMUL_INT128)
typedef uint64_t secp256k1_scalar_uint;
#elif defined(SECP256K1_WIDEMUL_INT64)
typedef uint32_t secp256k1_scalar_uint;
#else
#	error 
#endif

#ifndef _countof
#	define _countof(arr) sizeof(arr) / sizeof((arr)[0])
#endif

#define secp256k1_scalar_WordBits (sizeof(secp256k1_scalar_uint) * 8)

// This nasty macro is under MIT license (afaik)
#if !defined(__LITTLE_ENDIAN__) && !defined(__BIG_ENDIAN__)
#  if (defined(__BYTE_ORDER__)  && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__) || \
     (defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN) || \
     (defined(_BYTE_ORDER) && _BYTE_ORDER == _BIG_ENDIAN) || \
     (defined(BYTE_ORDER) && BYTE_ORDER == BIG_ENDIAN) || \
     (defined(__sun) && defined(__SVR4) && defined(_BIG_ENDIAN)) || \
     defined(__ARMEB__) || defined(__THUMBEB__) || defined(__AARCH64EB__) || \
     defined(_MIBSEB) || defined(__MIBSEB) || defined(__MIBSEB__) || \
     defined(_M_PPC)
#        define __BIG_ENDIAN__
#  elif (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) || /* gcc */\
     (defined(__BYTE_ORDER) && __BYTE_ORDER == __LITTLE_ENDIAN) /* linux header */ || \
     (defined(_BYTE_ORDER) && _BYTE_ORDER == _LITTLE_ENDIAN) || \
     (defined(BYTE_ORDER) && BYTE_ORDER == LITTLE_ENDIAN) /* mingw header */ ||  \
     (defined(__sun) && defined(__SVR4) && defined(_LITTLE_ENDIAN)) || /* solaris */ \
     defined(__ARMEL__) || defined(__THUMBEL__) || defined(__AARCH64EL__) || \
     defined(_MIPSEL) || defined(__MIPSEL) || defined(__MIPSEL__) || \
     defined(_M_IX86) || defined(_M_X64) || defined(_M_IA64) || /* msvc for intel processors */ \
     defined(_M_ARM) /* msvc code on arm executes in little endian mode */
#        define __LITTLE_ENDIAN__
#  elif
#    error can not detect endian-ness
#  endif
#endif

#ifdef _MSC_VER

	inline uint16_t bswap16(uint16_t x) { return _byteswap_ushort(x); }
	inline uint32_t bswap32(uint32_t x) { static_assert(sizeof(uint32_t) == sizeof(unsigned long), ""); return _byteswap_ulong(x); }
	inline uint64_t bswap64(uint64_t x) { return _byteswap_uint64(x); }

#else // _MSC_VER

	uint16_t bswap16(uint16_t x) { return __builtin_bswap16(x); }
	uint32_t bswap32(uint32_t x) { return __builtin_bswap32(x); }
	uint64_t bswap64(uint64_t x) { return __builtin_bswap64(x); }

#endif // _MSC_VER

#ifdef __LITTLE_ENDIAN__

	uint16_t bswap16_be(uint16_t x) { return bswap16(x); }
	uint32_t bswap32_be(uint32_t x) { return bswap32(x); }
	uint64_t bswap64_be(uint64_t x) { return bswap64(x); }

	uint16_t bswap16_le(uint16_t x) { return x; }
	uint32_t bswap32_le(uint32_t x) { return x; }
	uint64_t bswap64_le(uint64_t x) { return x; }

#else // __LITTLE_ENDIAN__

	inline uint16_t bswap16_le(uint16_t x) { return bswap16(x); }
	inline uint32_t bswap32_le(uint32_t x) { return bswap32(x); }
	inline uint64_t bswap64_le(uint64_t x) { return bswap64(x); }

	inline uint16_t bswap16_be(uint16_t x) { return x; }
	inline uint32_t bswap32_be(uint32_t x) { return x; }
	inline uint64_t bswap64_be(uint64_t x) { return x; }

#endif // __LITTLE_ENDIAN__

#ifdef TARGET_NANOS

uint16_t g_SufferPoints = 0;

void OnSuffered();

void Suffer(uint16_t n)
{
	if (g_SufferPoints)
	{
		g_SufferPoints += n;
		if (g_SufferPoints >= 2000)
		{
			g_SufferPoints = 1;
			OnSuffered();
		}
	}
}

#else // TARGET_NANOS

void Suffer(uint16_t n)
{
	UNUSED(n);
}

#endif // TARGET_NANOS

#ifdef BeamCrypto_ExternalGej


typedef gej_t CustomGenerator;

void Gej_Mul(gej_t* p, const gej_t* a, const secp256k1_scalar* k, int bFast)
{
	UintBig s;
	secp256k1_scalar_get_b32(s.m_pVal, k);

	Gej_Mul_Ub(p, a, &s, bFast);

	if (!bFast)
		SECURE_ERASE_OBJ(s);
}


void Gej_MulFast(gej_t* p, const gej_t* a, const secp256k1_scalar* k)
{
	Gej_Mul(p, a, k, 1);
}

void Gej_MulSecure(gej_t* p, const gej_t* a, const secp256k1_scalar* k)
{
	Gej_Mul(p, a, k, 0);
}

void Point_Gej_from_Ge(gej_t* p, const secp256k1_ge* pGe)
{
	if (secp256k1_ge_is_infinity(pGe))
		Gej_Destroy(p);
	else
	{
		AffinePoint ap;

		secp256k1_fe_normalize((secp256k1_fe*) &pGe->x); // seems unnecessary, but ok
		secp256k1_fe_normalize((secp256k1_fe*) &pGe->y);

		secp256k1_fe_get_b32(ap.m_X.m_pVal, &pGe->x);
		secp256k1_fe_get_b32(ap.m_Y.m_pVal, &pGe->y);

		Gej_Set_Affine(p, &ap);
	}
}

void Point_Ge_from_Affine(secp256k1_ge* pGe, const AffinePoint* pAp)
{
	secp256k1_fe_set_b32_mod(&pGe->x, pAp->m_X.m_pVal);
	secp256k1_fe_set_b32_mod(&pGe->y, pAp->m_Y.m_pVal);
	pGe->infinity = 0;
}

__stack_hungry__
void Point_Ge_from_Gej(secp256k1_ge* pGe, const gej_t* p)
{
	if (Gej_Is_infinity(p))
		pGe->infinity = 1; // no specific function like secp256k1_ge_set_infinity
	else
	{
		AffinePoint ap;
		Gej_Get_Affine(p, &ap);

		Point_Ge_from_Affine(pGe, &ap);
	}
}

#else // BeamCrypto_ExternalGej

typedef secp256k1_gej gej_t;

void Gej_Init(gej_t* p)
{
	UNUSED(p);
}

void Gej_Destroy(gej_t* p)
{
	UNUSED(p);
}

int Gej_Is_infinity(const gej_t* p)
{
	return secp256k1_gej_is_infinity(p);
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

inline static secp256k1_scalar_uint BitWalker_xor(const BitWalker* p, secp256k1_scalar* pK)
{
	return (pK->d[p->m_Word] ^= p->m_Msk) & p->m_Msk;
}

#define c_WNaf_Invalid 0x80
static_assert((c_MultiMac_OddCount(c_MultiMac_nBits_Rangeproof) * 2) < c_WNaf_Invalid, "");
static_assert((c_MultiMac_OddCount(c_MultiMac_nBits_H) * 2) < c_WNaf_Invalid, "");
static_assert((c_MultiMac_OddCount(c_MultiMac_nBits_Custom) * 2) < c_WNaf_Invalid, "");

inline static void WNaf_Cursor_SetInvalid(MultiMac_WNaf* p)
{
	p->m_iBit = 0xff;
	p->m_iElement = c_WNaf_Invalid;
}


static int WNaf_Cursor_Init(MultiMac_WNaf* p, secp256k1_scalar* pK, unsigned int nMaxWnd)
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
				assert(nWndLen <= nMaxWnd);

				if (val)
					p->m_iElement |= (1 << (nWndLen - 1));

				if (++nWndLen > nMaxWnd)
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

static void WNaf_Cursor_MoveNext(MultiMac_WNaf* p, const secp256k1_scalar* pK, unsigned int nMaxWnd)
{
	if (p->m_iBit <= nMaxWnd)
		return;

	BitWalker bw;
	BitWalker_SetPos(&bw, --p->m_iBit);

	// find next nnz bit
	for (; ; BitWalker_MoveDown(&bw), --p->m_iBit)
	{
		if (BitWalker_get(&bw, pK))
			break;

		if (p->m_iBit <= nMaxWnd)
		{
			WNaf_Cursor_SetInvalid(p);
			return;
		}
	}

	p->m_iBit -= nMaxWnd;
	p->m_iElement = 0;

	for (unsigned int i = 0; i < (nMaxWnd - 1); i++)
	{
		p->m_iElement <<= 1;

		BitWalker_MoveDown(&bw);
		if (BitWalker_get(&bw, pK))
			p->m_iElement |= 1;
	}

	unsigned int nMaxElements = c_MultiMac_OddCount(nMaxWnd);

	assert(p->m_iElement < nMaxElements);

	// last indicator bit
	BitWalker_MoveDown(&bw);
	if (!BitWalker_get(&bw, pK))
		// must negate instead of addition
		p->m_iElement += nMaxElements;
}

static void MultiMac_Calculate_LoadFast(const MultiMac_Context* p, secp256k1_ge* pGe, unsigned int iGen, unsigned int iElem)
{
	unsigned int nPitch = c_MultiMac_OddCount(p->m_Fast.m_WndBits);
	assert(iElem < nPitch);

	unsigned int n = nPitch * iGen + iElem;
	secp256k1_ge_from_storage(pGe, p->m_Fast.m_pGen0 + n);
}

#endif // BeamCrypto_ExternalGej

void Point_Gej_from_Ge(gej_t*, const secp256k1_ge*);
void Point_Ge_from_Gej(secp256k1_ge*, const gej_t*);

__stack_hungry__
static void wrap_gej_add_ge_var(gej_t* r, const gej_t* a, const secp256k1_ge* b /*, secp256k1_fe* rzr */)
{
#ifdef BeamCrypto_ExternalGej
	gej_t gej;
	Gej_Init(&gej);
	Point_Gej_from_Ge(&gej, b);
	Gej_Add(r, a, &gej);
	Gej_Destroy(&gej);

#else // BeamCrypto_ExternalGej
	secp256k1_gej_add_ge_var(r, a, b, 0 /* rzr */);
#endif // BeamCrypto_ExternalGej
	Suffer(12); // 9 mul, 3 sqr
}

__stack_hungry__
static void wrap_gej_add_var(gej_t* r, const gej_t* a, const gej_t* b /*, secp256k1_fe* rzr */)
{
#ifdef BeamCrypto_ExternalGej
	Gej_Add(r, a, b);
#else // BeamCrypto_ExternalGej
	secp256k1_gej_add_var(r, a, b, 0 /* rzr */);
#endif // BeamCrypto_ExternalGej
	Suffer(16); // 12 mul, 4 sqr
}

__stack_hungry__
static int wrap_scalar_add(secp256k1_scalar* r, const secp256k1_scalar* a, const secp256k1_scalar* b)
{
	return secp256k1_scalar_add(r, a, b);
}

static void wrap_scalar_mul(secp256k1_scalar* r, const secp256k1_scalar* a, const secp256k1_scalar* b)
{
	secp256k1_scalar_mul(r, a, b);
	Suffer(2); // assume scalar multiplication is less optimized than fe
}

static void wrap_scalar_inverse(secp256k1_scalar* r, const secp256k1_scalar* a)
{
	secp256k1_scalar_inverse(r, a);
	Suffer(300); // very heavy: inverse + misc
}

void secp256k1_scalar_set_u64(secp256k1_scalar* r, uint64_t v)
{
#ifdef SECP256K1_WIDEMUL_INT128
	static_assert(sizeof(r->d[0]) == sizeof(v), "");
	r->d[0] = v;
#else // SECP256K1_WIDEMUL_INT128
	static_assert(sizeof(r->d[0]) * 2 == sizeof(v), "");
	r->d[0] = static_cast<uint32_t>(v);
	r->d[1] = static_cast<uint32_t>(v >> 32u);
#endif // SECP256K1_WIDEMUL_INT128

	memset(r->d + sizeof(v) / sizeof(r->d[0]), 0, sizeof(r->d) - sizeof(v));
}


#ifndef BeamCrypto_ExternalGej
static void wrap_fe_mul(secp256k1_fe* r, const secp256k1_fe* a, const secp256k1_fe* b)
{
	secp256k1_fe_mul(r, a, b);
	Suffer(1);
}

__stack_hungry__
static void wrap_ge_neg(secp256k1_ge* r, const secp256k1_ge* a)
{
	secp256k1_ge_neg(r, a);
}

__stack_hungry__
static void wrap_gej_double_var(gej_t* r, const gej_t* a/*, secp256k1_fe* rzr*/)
{
	secp256k1_gej_double_var(r, a, 0 /* rzr */);
	Suffer(7); // 3 mul, 4 sqr
}

__stack_hungry__
static void wrap_gej_add_zinv_var(gej_t* r, const gej_t* a, const secp256k1_ge* b, const secp256k1_fe* bzinv)
{
	secp256k1_gej_add_zinv_var(r, a, b, bzinv);
	Suffer(13); // 10 mul, 3 sqr
}

__stack_hungry__
static void MultiMac_Calculate_PrePhase(MultiMac_Context* const p)
{
	secp256k1_gej_set_infinity(p->m_pRes);

	if (!p->m_Fast.m_Count)
	{
		p->m_Fast.m_pZDenom = 0;
		return;
	}

	for (unsigned int i = 0; i < p->m_Fast.m_Count; i++)
	{
		MultiMac_WNaf* pWnaf = p->m_Fast.m_pWnaf + i;
		secp256k1_scalar* pS = p->m_Fast.m_pK + i;

		int carry = WNaf_Cursor_Init(pWnaf, pS, p->m_Fast.m_WndBits);
		if (carry)
		{
			secp256k1_ge ge;
			MultiMac_Calculate_LoadFast(p, &ge, i, 0);
			wrap_gej_add_ge_var(p->m_pRes, p->m_pRes, &ge);
		}
	}
}

__stack_hungry__
static void MultiMac_Calculate_Secure_Read(secp256k1_ge* pGe, const MultiMac_Secure* pGen, unsigned int iElement)
{
	secp256k1_ge_storage ges;
	ZERO_OBJ(ges); // suppress warning: potential unanitialized var used

	for (unsigned int j = 0; j < c_MultiMac_Secure_nCount; j++)
		secp256k1_ge_storage_cmov(&ges, pGen->m_pPt + j, iElement == j);

	secp256k1_ge_from_storage(pGe, &ges); // inline is ok here
	SECURE_ERASE_OBJ(ges);
}

__stack_hungry__
static void MultiMac_Calculate_SecureBit(const MultiMac_Context* p, unsigned int iBit)
{
	secp256k1_ge ge;

	static_assert(!(secp256k1_scalar_WordBits % c_MultiMac_nBits_Secure), "");

	unsigned int iWord = iBit / secp256k1_scalar_WordBits;
	unsigned int nShift = iBit % secp256k1_scalar_WordBits;
	const secp256k1_scalar_uint nMsk = ((1U << c_MultiMac_nBits_Secure) - 1);

	for (unsigned int i = 0; i < p->m_Secure.m_Count; i++)
	{
		unsigned int iElement = (unsigned int) ((p->m_Secure.m_pK[i].d[iWord] >> nShift) & nMsk);
		const MultiMac_Secure* pGen = p->m_Secure.m_pGen + i;

		MultiMac_Calculate_Secure_Read(&ge, pGen, iElement);

		if (p->m_Fast.m_pZDenom)
			wrap_gej_add_zinv_var(p->m_pRes, p->m_pRes, &ge, p->m_Fast.m_pZDenom);
		else
			wrap_gej_add_ge_var(p->m_pRes, p->m_pRes, &ge);
	}
}

__stack_hungry__
static void MultiMac_Calculate_FastBit(const MultiMac_Context* p, unsigned int iBit)
{
	unsigned int nMaxWnd = p->m_Fast.m_WndBits;
	unsigned int nMaxElements = c_MultiMac_OddCount(nMaxWnd);

	for (unsigned int i = 0; i < p->m_Fast.m_Count; i++)
	{
		MultiMac_WNaf* pWnaf = p->m_Fast.m_pWnaf + i;

		if (((uint8_t) iBit) != pWnaf->m_iBit)
			continue;

		unsigned int iElem = pWnaf->m_iElement;

		if (c_WNaf_Invalid == iElem)
			continue;

		int bNegate = (iElem >= nMaxElements);
		if (bNegate)
		{
			iElem = (nMaxElements * 2 - 1) - iElem;
			assert(iElem < nMaxElements);
		}

		secp256k1_ge ge;
		MultiMac_Calculate_LoadFast(p, &ge, i, iElem);

		if (bNegate)
			wrap_ge_neg(&ge, &ge);

		wrap_gej_add_ge_var(p->m_pRes, p->m_pRes, &ge);

		WNaf_Cursor_MoveNext(pWnaf, p->m_Fast.m_pK + i, nMaxWnd);
	}
}

__stack_hungry__
static void MultiMac_Calculate_PostPhase(const MultiMac_Context* p)
{
	if (p->m_Fast.m_pZDenom)
		// fix denominator
		wrap_fe_mul(&p->m_pRes->z, &p->m_pRes->z, p->m_Fast.m_pZDenom);

	for (unsigned int i = 0; i < p->m_Secure.m_Count; i++)
	{
		secp256k1_ge ge;
		secp256k1_ge_from_storage(&ge, p->m_Secure.m_pGen[i].m_pPt + c_MultiMac_Secure_nCount);
		wrap_gej_add_ge_var(p->m_pRes, p->m_pRes, &ge);
	}

}

__stack_hungry__
void MultiMac_Calculate(MultiMac_Context* p)
{
	MultiMac_Calculate_PrePhase(p);

	for (unsigned int iBit = c_ECC_nBits; iBit--; )
	{
		wrap_gej_double_var(p->m_pRes, p->m_pRes); // would be fast if zero, no need to check explicitly

		if (!(iBit % c_MultiMac_nBits_Secure) && p->m_Secure.m_Count)
			MultiMac_Calculate_SecureBit(p, iBit);

		if (p->m_Fast.m_Count) // suppress the warning, potential usage of uninitialized m_Fast.m_WndBits
			MultiMac_Calculate_FastBit(p, iBit);
	}

	MultiMac_Calculate_PostPhase(p);
}

//////////////////////////////
// Batch normalization
__stack_hungry__
static void secp256k1_gej_rescale_To_ge(gej_t* pGej, const secp256k1_fe* pZ)
{
	// equivalent of secp256k1_gej_rescale, but doesn't change z coordinate
	// A bit more effective when the value of z is known in advance (such as when normalizing)

	// object is implicitly converted into ge
	secp256k1_ge* pGe = (secp256k1_ge *) pGej;

	secp256k1_fe zz;
	secp256k1_fe_sqr(&zz, pZ);
	Suffer(1);

	wrap_fe_mul(&pGe->x, &pGej->x, &zz);
	wrap_fe_mul(&pGe->y, &pGej->y, &zz);
	wrap_fe_mul(&pGe->y, &pGej->y, pZ);

	pGe->infinity = 0;
}

void Point_Gej_BatchRescale(gej_t*  pGej, unsigned int nCount, secp256k1_fe* pBuf, secp256k1_fe* pZDenom, int bNormalize)
{
	int iPrev = -1;
	for (unsigned int i = 0; i < nCount; i++)
	{
		if (secp256k1_gej_is_infinity(pGej + i))
		{
			((secp256k1_ge*)(pGej + i))->infinity = 1;
			continue;
		}

		if (iPrev >= 0)
			wrap_fe_mul(pBuf + i, pBuf + iPrev, &pGej[i].z);
		else
			pBuf[i] = pGej[i].z;

		iPrev = i;
	}

	if (iPrev < 0)
		return; // all are zero

	if (bNormalize)
	{
		secp256k1_fe_inv(pZDenom, pBuf + iPrev); // the only expensive call: 15 mul, 255 sqr
		Suffer(280); // Very heavy
	}
	else
		secp256k1_fe_set_int(pZDenom, 1); // can be arbitrary

	iPrev = -1;
	for (unsigned int i = nCount; i--; )
	{
		if (secp256k1_gej_is_infinity(pGej + i))
			continue;

		if (iPrev >= 0)
		{
			wrap_fe_mul(pBuf + iPrev, pBuf + i, pZDenom);
			wrap_fe_mul(pZDenom, pZDenom, &pGej[iPrev].z);

			secp256k1_gej_rescale_To_ge(pGej + iPrev, pBuf + iPrev);
		}

		iPrev = i;
	}

	assert(iPrev >= 0);

	secp256k1_gej_rescale_To_ge(pGej + iPrev, pZDenom);

	// if we're normalizing - no need to return the common denominator (it's assumed 1)
	// If we're just bringing to common denominator - we assume that 1st element is normalized by the caller, hence zDenom is already set

	/*
	if (bNormalize)
		secp256k1_gej_rescale_To_ge(pGej + iPrev, pZDenom);
	else
	{
		pBuf[iPrev] = *pZDenom;
		wrap_fe_mul(pZDenom, pZDenom, &pGej[iPrev].z);

		secp256k1_gej_rescale_To_ge(pGej + iPrev, pBuf + iPrev);
	}
	*/
}

__stack_hungry__
void Point_Gej_2_Normalize(gej_t* pGej)
{
	secp256k1_fe pBuf[2];
	secp256k1_fe zDenom;
	Point_Gej_BatchRescale(pGej, _countof(pBuf), pBuf, &zDenom, 1);
}

typedef struct
{
	secp256k1_ge_storage m_pPt[c_MultiMac_OddCount(c_MultiMac_nBits_Custom)]; // odd powers
	secp256k1_fe m_zDenom;
} CustomGenerator;

void Point_CalculateOdds(gej_t* pOdds, uint32_t n, const secp256k1_ge* pGe)
{
	assert(n);
	Point_Gej_from_Ge(pOdds, pGe);

	gej_t* const pX2 = pOdds + n - 1;
	wrap_gej_double_var(pX2, pOdds);

	for (uint32_t i = 1; i < n; i++)
	{
		wrap_gej_add_var(pOdds + i, pX2, pOdds + i - 1);
		assert(!secp256k1_gej_is_infinity(pOdds + i)); // odd powers of non-zero point must not be zero!
	}
}

void Point_Gej_ToCommonDenominator(gej_t* pOdds, uint32_t n, secp256k1_ge_storage* pRes, secp256k1_fe* pZDenom)
{
	// to common denominator
	static_assert(sizeof(secp256k1_fe) <= sizeof(secp256k1_ge_storage), "Need this to temporary use its memory");

	Point_Gej_BatchRescale(pOdds, n, (secp256k1_fe*) pRes, pZDenom, 0);

	for (unsigned int i = 0; i < n; i++)
		secp256k1_ge_to_storage(pRes + i, (secp256k1_ge*)(pOdds + i));
}

__stack_hungry__
void MultiMac_Fast_Custom_Init(CustomGenerator* p, const secp256k1_ge* pGe)
{
	assert(!secp256k1_ge_is_infinity(pGe));

	// calculate odd powers
	gej_t pOdds[_countof(p->m_pPt)];

	Point_CalculateOdds(pOdds, _countof(pOdds), pGe);

	Point_Gej_ToCommonDenominator(pOdds, _countof(pOdds), p->m_pPt, &p->m_zDenom);
}
#endif // BeamCrypto_ExternalGej

void secp256k1_hmac_sha256_write_UintBig(secp256k1_hmac_sha256_t* pHMac, const UintBig* p)
{
	secp256k1_hmac_sha256_write(pHMac, p->m_pVal, sizeof(p->m_pVal));
}

void secp256k1_hmac_sha256_write_CompactPoint(secp256k1_hmac_sha256_t* pHMac, const CompactPoint* pCompact)
{
	secp256k1_hmac_sha256_write_UintBig(pHMac, &pCompact->m_X);
	secp256k1_hmac_sha256_write(pHMac, &pCompact->m_Y, sizeof(pCompact->m_Y));
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
	secp256k1_hmac_sha256_write_UintBig(&hmac, pSeed);
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
		secp256k1_hmac_sha256_write_UintBig(&hmac, &p->m_Okm);

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

int IsUintBigZero(const UintBig* p)
{
	return memis0(p->m_pVal, sizeof(p->m_pVal));
}

//////////////////////////////
// Point
uint8_t Point_Compact_from_Ge_Ex(UintBig* pX, const secp256k1_ge* pGe)
{
	if (secp256k1_ge_is_infinity(pGe))
	{
		ZERO_OBJ(*pX);
		return 0;
	}

	secp256k1_fe_normalize((secp256k1_fe*) &pGe->x); // seems unnecessary, but ok
	secp256k1_fe_normalize((secp256k1_fe*) &pGe->y);

	secp256k1_fe_get_b32(pX->m_pVal, &pGe->x);
	return (secp256k1_fe_is_odd(&pGe->y) != 0);
}

void Point_Compact_from_Ge(CompactPoint* pCompact, const secp256k1_ge* pGe)
{
	pCompact->m_Y = Point_Compact_from_Ge_Ex(&pCompact->m_X, pGe);
}

void Point_Compact_from_Gej(CompactPoint* pCompact, const gej_t* pGej)
{
	secp256k1_ge ge;
	Point_Ge_from_Gej(&ge, pGej);
	Point_Compact_from_Ge(pCompact, &ge);
}

uint8_t Point_Compact_from_Gej_Ex(UintBig* pX, const gej_t* pGej)
{
	secp256k1_ge ge;
	Point_Ge_from_Gej(&ge, pGej);
	return Point_Compact_from_Ge_Ex(pX, &ge);
}

#ifndef BeamCrypto_ExternalGej
void Point_Gej_from_Ge(gej_t* pGej, const secp256k1_ge* pGe)
{
	secp256k1_gej_set_ge(pGej, pGe);
}
#endif // BeamCrypto_ExternalGej

int Point_Ge_from_CompactNnz(secp256k1_ge* pGe, const CompactPoint* pCompact)
{
	if (pCompact->m_Y > 1)
		return 0; // not well-formed

	if (!secp256k1_fe_set_b32_limit(&pGe->x, pCompact->m_X.m_pVal))
		return 0; // not well-formed

	if (!secp256k1_ge_set_xo_var(pGe, &pGe->x, pCompact->m_Y)) // according to code it seems ok to use ge.x as an argument
		return 0;

	return 1; // ok
}

int Point_Ge_from_Compact(secp256k1_ge* pGe, const CompactPoint* pCompact)
{
	if (!Point_Ge_from_CompactNnz(pGe, pCompact))
	{
		if (pCompact->m_Y || !IsUintBigZero(&pCompact->m_X))
			return 0;

		pGe->infinity = 1; // no specific function like secp256k1_ge_set_infinity
	}

	return 1;
}

#ifdef BeamCrypto_ExternalGej

void MulPoint(gej_t* pGej, const AffinePoint* pGen, const secp256k1_scalar* pK)
{
	Gej_Set_Affine(pGej, pGen);
	Gej_MulSecure(pGej, pGej, pK);
}

#else // BeamCrypto_ExternalGej

void MulPoint(gej_t* pGej, const MultiMac_Secure* pGen, const secp256k1_scalar* pK)
{
	MultiMac_Context ctx;
	ctx.m_pRes = pGej;
	ctx.m_Fast.m_Count = 0;
	ctx.m_Secure.m_Count = 1;
	ctx.m_Secure.m_pGen = pGen;
	ctx.m_Secure.m_pK = pK;

	MultiMac_Calculate(&ctx);
}

void Point_Ge_from_Gej(secp256k1_ge* pGe, const gej_t* pGej)
{
	secp256k1_ge_set_gej_var(pGe, (gej_t*) pGej); // expensive, better to a batch convertion
	Suffer(1000); // Very heavy
}

#endif // BeamCrypto_ExternalGej

void MulG(gej_t* pGej, const secp256k1_scalar* pK)
{
	MulPoint(pGej, Context_get()->m_pGenGJ, pK);
}

void MulJ(gej_t* pGej, const secp256k1_scalar* pK)
{
	MulPoint(pGej, Context_get()->m_pGenGJ + 1, pK);
}

__stack_hungry__
void Sk2Pk(UintBig* pRes, secp256k1_scalar* pK)
{
	gej_t gej;
	Gej_Init(&gej);
	MulG(&gej, pK);

	uint8_t y = Point_Compact_from_Gej_Ex(pRes, &gej);
	if (y)
		secp256k1_scalar_negate(pK, pK);

	Gej_Destroy(&gej);
}

void secp256k1_sha256_write_UintBig(secp256k1_sha256_t* pSha, const UintBig* p)
{
	secp256k1_sha256_write(pSha, p->m_pVal, sizeof(p->m_pVal));
}

void secp256k1_sha256_write_u8(secp256k1_sha256_t* pSha, uint8_t x)
{
	secp256k1_sha256_write(pSha, &x, sizeof(x));
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
	secp256k1_sha256_write_UintBig(&p->m_sha, pHash);
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

void Oracle_NextPoint(Oracle* p, CompactPoint* pCompact, secp256k1_ge* pGe)
{
	pCompact->m_Y = 0;

	while (1)
	{
		Oracle_NextHash(p, &pCompact->m_X);

		if (Point_Ge_from_CompactNnz(pGe, pCompact))
			break;
	}
}

//////////////////////////////
// CoinID
#define c_CoinID_nSubkeyBits 24

uint32_t CoinID_getSubkey(const CoinID* p)
{
	return p->m_SubIdx & ((1U << c_CoinID_nSubkeyBits) - 1);
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

		secp256k1_sha256_write_u8(pSha, x);

	} while (nContinue);
}

void secp256k1_sha256_write_CompactPoint(secp256k1_sha256_t* pSha, const CompactPoint* pCompact)
{
	secp256k1_sha256_write_UintBig(pSha, &pCompact->m_X);
	secp256k1_sha256_write_u8(pSha, pCompact->m_Y);
}

void secp256k1_sha256_write_CompactPointOptional2(secp256k1_sha256_t* pSha, const CompactPoint* pCompact, uint8_t bValid)
{
	secp256k1_sha256_write_u8(pSha, bValid);
	if (bValid)
		secp256k1_sha256_write_CompactPoint(pSha, pCompact);
}

void secp256k1_sha256_write_CompactPointOptional(secp256k1_sha256_t* pSha, const CompactPoint* pCompact)
{
	secp256k1_sha256_write_CompactPointOptional2(pSha, pCompact, !!pCompact);
}

void secp256k1_sha256_write_CompactPointEx(secp256k1_sha256_t* pSha, const UintBig* pX, uint8_t nY)
{
	secp256k1_sha256_write_UintBig(pSha, pX);
	secp256k1_sha256_write_u8(pSha, nY & 1);
}

__stack_hungry__
void secp256k1_sha256_write_Ge(secp256k1_sha256_t* pSha, const secp256k1_ge* pGe)
{
	CompactPoint pt;
	Point_Compact_from_Ge(&pt, pGe);

	secp256k1_sha256_write_CompactPoint(pSha, &pt);
}

void secp256k1_sha256_write_Gej(secp256k1_sha256_t* pSha, const gej_t* pGej) // expensive
{
	secp256k1_ge ge;
	Point_Ge_from_Gej(&ge, pGej);
	secp256k1_sha256_write_Ge(pSha, &ge);
}

void secp256k1_sha256_write_Gej_converted(secp256k1_sha256_t* pSha, const gej_t* pGej)
{
#ifdef BeamCrypto_ExternalGej
	secp256k1_sha256_write_Gej(pSha, pGej);
#else // BeamCrypto_ExternalGej
	secp256k1_sha256_write_Ge(pSha, (secp256k1_ge*)pGej);
#endif // BeamCrypto_ExternalGej
}

__stack_hungry__
void CoinID_getHash(const CoinID* p, UintBig* pHash)
{
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);

	HASH_WRITE_STR(sha, "kidv-1");
	secp256k1_sha256_write_Num(&sha, p->m_Idx);
	secp256k1_sha256_write_Num(&sha, p->m_Type);
	secp256k1_sha256_write_Num(&sha, p->m_SubIdx);
	// newer scheme - account for the Value and Asset.
	secp256k1_sha256_write_Num(&sha, p->m_Amount);

	if (p->m_AssetID)
	{
		HASH_WRITE_STR(sha, "asset");
		secp256k1_sha256_write_Num(&sha, p->m_AssetID);
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
void KdfRaw_Derive_PKey(const UintBig* pSecret, const UintBig* pHv, secp256k1_scalar* pK)
{
	NonceGenerator ng;

	static const char szSalt[] = "beam-Key";

	secp256k1_hmac_sha256_t hmac;
	NonceGenerator_InitBegin(&ng, &hmac, szSalt, sizeof(szSalt));

	secp256k1_hmac_sha256_write_UintBig(&hmac, pSecret);
	secp256k1_hmac_sha256_write_UintBig(&hmac, pHv);
	NonceGenerator_InitEnd(&ng, &hmac);

	NonceGenerator_NextScalar(&ng, pK);

	SECURE_ERASE_OBJ(hmac);
	SECURE_ERASE_OBJ(ng);
}

void Kdf_Derive_PKey(const Kdf* p, const UintBig* pHv, secp256k1_scalar* pK)
{
	KdfRaw_Derive_PKey(&p->m_Secret, pHv, pK);
}

void Kdf_Derive_SKey(const Kdf* p, const UintBig* pHv, secp256k1_scalar* pK)
{
	Kdf_Derive_PKey(p, pHv, pK);
	wrap_scalar_mul(pK, pK, &p->m_kCoFactor);
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
__stack_hungry__
void CoinID_GetAssetGen(AssetID aid, secp256k1_ge* pGe)
{
	assert(aid);

	Oracle oracle;
	Oracle_Init(&oracle);

	HASH_WRITE_STR(oracle.m_sha, "B.Asset.Gen.V1");
	secp256k1_sha256_write_Num(&oracle.m_sha, aid);

	CompactPoint pt;
	Oracle_NextPoint(&oracle, &pt, pGe);
}

__stack_hungry__
void CoinID_GenerateAGen(AssetID aid, CustomGenerator* pAGen)
{
	assert(aid);

#ifdef BeamCrypto_ExternalGej

	secp256k1_ge ge;
	CoinID_GetAssetGen(aid, &ge);
	Point_Gej_from_Ge(pAGen, &ge);

#else // BeamCrypto_ExternalGej

	static_assert(sizeof(*pAGen) >= sizeof(secp256k1_ge), "");

	CoinID_GetAssetGen(aid, (secp256k1_ge*) pAGen);
	MultiMac_Fast_Custom_Init(pAGen, (secp256k1_ge*) pAGen);

#endif // BeamCrypto_ExternalGej
}

__stack_hungry__
void CoinID_getCommRawEx(const secp256k1_scalar* pkG, secp256k1_scalar* pkH, const CustomGenerator* pAGen, gej_t* pGej)
{
	Context* pCtx = Context_get();

	// sk*G + v*H

#ifdef BeamCrypto_ExternalGej

	MulG(pGej, pkG);

	gej_t gej;
	Gej_Init(&gej);

	if (!pAGen)
	{
		Gej_Set_Affine(&gej, &pCtx->m_GenH);
		pAGen = &gej;
	}

	Gej_MulFast(&gej, pAGen, pkH);
	Gej_Add(pGej, pGej, &gej);

	Gej_Destroy(&gej);

#else // BeamCrypto_ExternalGej

	MultiMac_WNaf wnaf;
	MultiMac_Context mmCtx;
	mmCtx.m_pRes = pGej;
	mmCtx.m_Secure.m_Count = 1;
	mmCtx.m_Secure.m_pK = pkG;
	mmCtx.m_Secure.m_pGen = pCtx->m_pGenGJ;
	mmCtx.m_Fast.m_Count = 1;
	mmCtx.m_Fast.m_pK = pkH;
	mmCtx.m_Fast.m_pWnaf = &wnaf;

	if (pAGen)
	{
		mmCtx.m_Fast.m_pGen0 = pAGen->m_pPt;
		mmCtx.m_Fast.m_WndBits = c_MultiMac_nBits_Custom;
		mmCtx.m_Fast.m_pZDenom = &pAGen->m_zDenom;
	}
	else
	{
		mmCtx.m_Fast.m_pGen0 = pCtx->m_pGenH;
		mmCtx.m_Fast.m_WndBits = c_MultiMac_nBits_H;
		mmCtx.m_Fast.m_pZDenom = 0;
	}

	MultiMac_Calculate(&mmCtx);
#endif // BeamCrypto_ExternalGej
}

//__stack_hungry__
void CoinID_getCommRaw(const secp256k1_scalar* pK, Amount amount, const CustomGenerator* pAGen, gej_t* pGej)
{
	secp256k1_scalar kH;
	secp256k1_scalar_set_u64(&kH, amount);
	CoinID_getCommRawEx(pK, &kH, pAGen, pGej);
}

__stack_hungry__
static void CoinID_getSkNonSwitch(const Kdf* pKdf, const CoinID* pCid, secp256k1_scalar* pK)
{
	UintBig hv;
	CoinID_getHash(pCid, &hv);

	Kdf kdfC;
	Kdf_getChild(&kdfC, CoinID_getSubkey(pCid), pKdf);

	Kdf_Derive_SKey(&kdfC, &hv, pK);
	SECURE_ERASE_OBJ(kdfC);
}

__stack_hungry__
static void CoinID_getSkSwitchDelta(secp256k1_scalar* pK, const gej_t* pCommsNorm)
{
	Oracle oracle;
	Oracle_Init(&oracle);

	secp256k1_sha256_write_Gej_converted(&oracle.m_sha, pCommsNorm);
	secp256k1_sha256_write_Gej_converted(&oracle.m_sha, pCommsNorm + 1);

	Oracle_NextScalar(&oracle, pK);
}

__stack_hungry__
static void CoinID_getSkComm_FromNonSwitchK(const CoinID* pCid, secp256k1_scalar* pK, CompactPoint* pComm, const CustomGenerator* pAGen)
{
	gej_t pGej[2];
	Gej_Init(pGej);
	Gej_Init(pGej + 1);

	CoinID_getCommRaw(pK, pCid->m_Amount, pAGen, pGej); // sk*G + amount*H(aid)
	MulJ(pGej + 1, pK); // sk*J

#ifndef BeamCrypto_ExternalGej
	Point_Gej_2_Normalize(pGej);
#endif // BeamCrypto_ExternalGej

	secp256k1_scalar kDelta;
	CoinID_getSkSwitchDelta(&kDelta, pGej);

	secp256k1_scalar_add(pK, pK, &kDelta);

	if (pComm)
	{
		MulG(pGej + 1, &kDelta);

#ifdef BeamCrypto_ExternalGej
		wrap_gej_add_var(pGej + 1, pGej + 1, pGej);
#else // BeamCrypto_ExternalGej
		// pGej[0] is ge
		wrap_gej_add_ge_var(pGej + 1, pGej + 1, (secp256k1_ge*) pGej);
#endif // BeamCrypto_ExternalGej

		Point_Compact_from_Gej(pComm, pGej + 1);
	}

	Gej_Destroy(pGej + 1);
	Gej_Destroy(pGej);
}

__stack_hungry__
void CoinID_getSkComm(const Kdf* pKdf, const CoinID* pCid, secp256k1_scalar* pK, CompactPoint* pComm)
{
	CoinID_getSkNonSwitch(pKdf, pCid, pK);

	CustomGenerator aGen;
#ifdef BeamCrypto_ExternalGej
	Gej_Init(&aGen);
#endif // BeamCrypto_ExternalGej

	if (pCid->m_AssetID)
		CoinID_GenerateAGen(pCid->m_AssetID, &aGen);

	CoinID_getSkComm_FromNonSwitchK(pCid, pK, pComm, pCid->m_AssetID ? &aGen : 0);

#ifdef BeamCrypto_ExternalGej
	Gej_Destroy(&aGen);
#endif // BeamCrypto_ExternalGej
}

__stack_hungry__
static void ShieldedInput_getSk(const KeyKeeper* p, const ShieldedInput_Blob* pInpBlob, const ShieldedInput_Fmt* pInpFmt, secp256k1_scalar* pK)
{
	UintBig hv;
	secp256k1_sha256_t sha;

	secp256k1_sha256_initialize(&sha);
	HASH_WRITE_STR(sha, "sh.skout");
	secp256k1_sha256_write_Num(&sha, pInpFmt->m_Amount);
	secp256k1_sha256_write_Num(&sha, pInpFmt->m_AssetID);
	secp256k1_sha256_write_Num(&sha, pInpFmt->m_Fee);
	secp256k1_sha256_write_UintBig(&sha, &pInpBlob->m_kSerG);
	secp256k1_sha256_write_Num(&sha, !!pInpBlob->m_IsCreatedByViewer);
	secp256k1_sha256_write_Num(&sha, pInpFmt->m_nViewerIdx);
	secp256k1_sha256_finalize(&sha, hv.m_pVal);

	Kdf kdfChild;
	Kdf_getChild(&kdfChild, c_ShieldedInput_ChildKdf, &p->m_MasterKey);
	Kdf_Derive_SKey(&kdfChild, &hv, pK);
}

//////////////////////////////
// RangeProof
typedef struct
{
	NonceGenerator m_NonceGen; // 88 bytes
	gej_t m_pGej[2]; // 248 bytes

	// 97 bytes. This can be saved, at expense of calculating them again (CoinID_getSkComm)
	secp256k1_scalar m_sk;
	secp256k1_scalar m_alpha;
	CompactPoint m_Commitment;

} RangeProof_Worker;

__stack_hungry__
static void RangeProof_Calculate_Before_S(RangeProof* const p, RangeProof_Worker* const pWrk)
{
	Oracle oracle;
	UintBig hv;
	secp256k1_scalar k;

	// get seed
	secp256k1_sha256_initialize(&oracle.m_sha);
	secp256k1_sha256_write_CompactPoint(&oracle.m_sha, &pWrk->m_Commitment);
	secp256k1_sha256_finalize(&oracle.m_sha, hv.m_pVal);

	Kdf_Derive_PKey(p->m_pKdf, &hv, &k);
	secp256k1_scalar_get_b32(hv.m_pVal, &k);

	secp256k1_sha256_initialize(&oracle.m_sha);
	secp256k1_sha256_write_UintBig(&oracle.m_sha, &hv);
	secp256k1_sha256_finalize(&oracle.m_sha, hv.m_pVal);

	// NonceGen
	static const char szSalt[] = "bulletproof";
	NonceGenerator_Init(&pWrk->m_NonceGen, szSalt, sizeof(szSalt), &hv);

	NonceGenerator_NextScalar(&pWrk->m_NonceGen, &pWrk->m_alpha); // alpha

	// embed params into alpha
#pragma pack (push, 1)
	typedef struct
	{
		uint32_t m_Padding;
		AssetID m_AssetID;
		uint64_t m_Idx;
		uint32_t m_Type;
		uint32_t m_SubIdx;
		Amount m_Amount;
	} RangeProof_Embedded;
#pragma pack (pop)

	static_assert(sizeof(RangeProof_Embedded) == c_ECC_nBytes, "");
	RangeProof_Embedded* pEmb = (RangeProof_Embedded*) hv.m_pVal;

	pEmb->m_Amount = bswap64_be(p->m_Cid.m_Amount);
	pEmb->m_SubIdx = bswap32_be(p->m_Cid.m_SubIdx);
	pEmb->m_Type = bswap32_be(p->m_Cid.m_Type);
	pEmb->m_Idx = bswap64_be(p->m_Cid.m_Idx);
	pEmb->m_AssetID = bswap32_be(p->m_Cid.m_AssetID);
	pEmb->m_Padding = 0;

	int overflow;
	secp256k1_scalar_set_b32(&k, hv.m_pVal, &overflow);
	assert(!overflow);

	secp256k1_scalar_add(&pWrk->m_alpha, &pWrk->m_alpha, &k);
}

__stack_hungry__
static void RangeProof_Calculate_S(RangeProof* const p, RangeProof_Worker* const pWrk)
{
	// Data buffers needed for calculating Part1.S
	// Need to multi-exponentiate nDims * 2 == 128 elements.
	// Calculating everything in a single pass is faster, but requires more buffers (stack memory)
	// Each element size is sizeof(secp256k1_scalar) + sizeof(MultiMac_WNaf) == 34 bytes
	//
	// This requires of 4.25K stack memory
#define nDims (sizeof(Amount) * 8)
#define Calc_S_Naggle_Max (nDims * 2)

#ifdef BeamCrypto_ScarceStack
#	define Calc_S_Naggle 22 // would take 6 iterations
#else // BeamCrypto_ScarceStack
#	define Calc_S_Naggle Calc_S_Naggle_Max // use max
#endif // BeamCrypto_ScarceStack

	static_assert(Calc_S_Naggle <= Calc_S_Naggle_Max, "Naggle too large");

#ifdef BeamCrypto_ExternalGej

	secp256k1_scalar s;
	NonceGenerator_NextScalar(&pWrk->m_NonceGen, &s);
	MulG(pWrk->m_pGej + 1, &s); // can mul fast!

	gej_t gej1, gej2;
	Gej_Init(&gej1);
	Gej_Init(&gej2);

	UintBig pK[2];

#else // BeamCrypto_ExternalGej

	secp256k1_scalar pS[Calc_S_Naggle];
	MultiMac_WNaf pWnaf[Calc_S_Naggle];

	secp256k1_scalar* const pRho = (secp256k1_scalar*) (pWrk->m_pGej + (Calc_S_Naggle < Calc_S_Naggle_Max)); // reuse its mem
	// depending on naggling, the 1st point would be either pGej o pGej+1, hence we may use the other one

	NonceGenerator_NextScalar(&pWrk->m_NonceGen, pRho);

	MultiMac_Context mmCtx;

	mmCtx.m_Secure.m_Count = 1;
	mmCtx.m_Secure.m_pK = pRho;
	mmCtx.m_Secure.m_pGen = Context_get()->m_pGenGJ;

	mmCtx.m_Fast.m_pZDenom = 0;
	mmCtx.m_Fast.m_Count = 0;
	mmCtx.m_Fast.m_WndBits = c_MultiMac_nBits_Rangeproof;
	mmCtx.m_Fast.m_pK = pS;
	mmCtx.m_Fast.m_pWnaf = pWnaf;
	mmCtx.m_Fast.m_pGen0 = Context_get()->m_pGenRangeproof[0];

#endif // BeamCrypto_ExternalGej

	for (unsigned int iBit = 0; iBit < nDims * 2; iBit++)
	{
#ifdef BeamCrypto_ExternalGej
		secp256k1_scalar* const pTrg = &s;
#else // BeamCrypto_ExternalGej

		if (Calc_S_Naggle == mmCtx.m_Fast.m_Count)
		{
			// flush
			mmCtx.m_pRes = pWrk->m_pGej + (iBit != Calc_S_Naggle); // 1st flush goes to pGej[0] directly
			MultiMac_Calculate(&mmCtx);

			if (iBit != Calc_S_Naggle)
				wrap_gej_add_var(pWrk->m_pGej, pWrk->m_pGej + 1, pWrk->m_pGej);

			mmCtx.m_Secure.m_Count = 0;
			mmCtx.m_Fast.m_Count = 0;
			mmCtx.m_Fast.m_pGen0 += Calc_S_Naggle * c_MultiMac_OddCount(c_MultiMac_nBits_Rangeproof);
		}

		secp256k1_scalar* const pTrg = pS + mmCtx.m_Fast.m_Count;
		mmCtx.m_Fast.m_Count++;

#endif // BeamCrypto_ExternalGej

		NonceGenerator_NextScalar(&pWrk->m_NonceGen, pTrg);

		if (!(iBit % nDims) && p->m_pKExtra)
		{
			// embed more info
			int overflow;
			secp256k1_scalar_set_b32(p->m_pTauX, p->m_pKExtra[iBit / nDims].m_pVal, &overflow);
			wrap_scalar_add(pTrg, pTrg, p->m_pTauX);
		}

#ifdef BeamCrypto_ExternalGej

		secp256k1_scalar_get_b32(pK[1 & iBit].m_pVal, &s);

		if (1 & iBit)
		{
			Gej_Set_Affine(&gej1, Context_get()->m_pGenRangeproof[iBit - 1]);
			Gej_Set_Affine(&gej2, Context_get()->m_pGenRangeproof[iBit]);

			Gej_Mul2_Fast(pWrk->m_pGej, &gej1, pK, &gej2, pK + 1);

			Gej_Add(pWrk->m_pGej + 1, pWrk->m_pGej + 1, pWrk->m_pGej);
		}

#endif // BeamCrypto_ExternalGej
	}

#ifdef BeamCrypto_ExternalGej

	Gej_Destroy(&gej2);
	Gej_Destroy(&gej1);

#else // BeamCrypto_ExternalGej

	mmCtx.m_pRes = pWrk->m_pGej + 1;
	MultiMac_Calculate(&mmCtx);

	if (Calc_S_Naggle < Calc_S_Naggle_Max)
		wrap_gej_add_var(pWrk->m_pGej + 1, pWrk->m_pGej + 1, pWrk->m_pGej);

#endif // BeamCrypto_ExternalGej
}

static void RangeProof_Calculate_A_Bits(gej_t* pRes, secp256k1_ge* pGeTmp, Amount v)
{
	Context* pCtx = Context_get();

#ifdef BeamCrypto_ExternalGej

	gej_t gej;
	Gej_Init(&gej);

	for (uint32_t i = 0; i < nDims; i++)
	{
		if (1 & (v >> i))
			Gej_Set_Affine(&gej, pCtx->m_pGenRangeproof[i]);
		else
		{
			Point_Ge_from_Affine(pGeTmp, pCtx->m_pGenRangeproof[nDims + i]);
			secp256k1_ge_neg(pGeTmp, pGeTmp);
			Point_Gej_from_Ge(&gej, pGeTmp);
		}

		Gej_Add(pRes, pRes, &gej);
	}

	Gej_Destroy(&gej);

#else // BeamCrypto_ExternalGej

	for (uint32_t i = 0; i < nDims; i++)
	{
		if (1 & (v >> i))
			secp256k1_ge_from_storage(pGeTmp, pCtx->m_pGenRangeproof[i]);
		else
		{
			secp256k1_ge_from_storage(pGeTmp, pCtx->m_pGenRangeproof[nDims + i]);
			secp256k1_ge_neg(pGeTmp, pGeTmp);
		}

		wrap_gej_add_ge_var(pRes, pRes, pGeTmp);
	}

#endif // BeamCrypto_ExternalGej
}

__stack_hungry__
static int RangeProof_Calculate_After_S(RangeProof* const p, RangeProof_Worker* const pWrk)
{
	{
		// CalcA
		MulG(pWrk->m_pGej, &pWrk->m_alpha); // alpha*G

		secp256k1_ge geTmp;
		RangeProof_Calculate_A_Bits(pWrk->m_pGej, &geTmp, p->m_Cid.m_Amount);
	}

#ifndef BeamCrypto_ExternalGej
	// normalize A,S at once, feed them to Oracle, get the challenges
	Point_Gej_2_Normalize(pWrk->m_pGej);
#endif // BeamCrypto_ExternalGej

	secp256k1_scalar pK[2];

	Oracle oracle;
	Oracle_Init(&oracle);
	secp256k1_sha256_write_Num(&oracle.m_sha, 0); // incubation time, must be zero
	secp256k1_sha256_write_CompactPoint(&oracle.m_sha, &pWrk->m_Commitment); // starting from Fork1, earlier schem is not allowed
	secp256k1_sha256_write_CompactPointOptional(&oracle.m_sha, p->m_pAssetGen); // starting from Fork3, earlier schem is not allowed

	for (unsigned int i = 0; i < 2; i++)
		secp256k1_sha256_write_Gej_converted(&oracle.m_sha, pWrk->m_pGej + i);

	for (unsigned int i = 0; i < 2; i++)
		Oracle_NextScalar(&oracle, pK + i); // challenges y,z. The 'y' is not needed

	{
		// Use the challenges, sk, T1 and T2 to init the NonceGen for blinding the sk
		static const char szSalt[] = "bulletproof-sk";

		secp256k1_hmac_sha256_t hmac;
		NonceGenerator_InitBegin(&pWrk->m_NonceGen, &hmac, szSalt, sizeof(szSalt));

		UintBig hv;
		secp256k1_scalar_get_b32(hv.m_pVal, &pWrk->m_sk);
		secp256k1_hmac_sha256_write_UintBig(&hmac, &hv);

		for (unsigned int i = 0; i < 2; i++)
		{
			secp256k1_hmac_sha256_write_CompactPoint(&hmac, p->m_pT_In + i);

			secp256k1_scalar_get_b32(hv.m_pVal, pK + i);
			secp256k1_hmac_sha256_write_UintBig(&hmac, &hv);
		}

		NonceGenerator_InitEnd(&pWrk->m_NonceGen, &hmac);
	}

	int ok = 1;

	secp256k1_scalar zChallenge = pK[1];

	for (unsigned int i = 0; i < 2; i++)
	{
		NonceGenerator_NextScalar(&pWrk->m_NonceGen, pK + i); // tau1/2

		MulG(pWrk->m_pGej + i, pK + i); // pub nonces of T1/T2

		secp256k1_ge ge;
		if (!Point_Ge_from_Compact(&ge, p->m_pT_In + i))
		{
			ok = 0;
			break;
		}

		wrap_gej_add_ge_var(pWrk->m_pGej + i, pWrk->m_pGej + i, &ge);
	}

	SECURE_ERASE_OBJ(pWrk->m_NonceGen);

	if (ok)
	{
		// normalize & expose
#ifndef BeamCrypto_ExternalGej
		Point_Gej_2_Normalize(pWrk->m_pGej);
#endif // BeamCrypto_ExternalGej

		for (unsigned int i = 0; i < 2; i++)
		{
#ifdef BeamCrypto_ExternalGej
			Point_Compact_from_Gej(p->m_pT_Out + i, pWrk->m_pGej + i);
#else // BeamCrypto_ExternalGej
			Point_Compact_from_Ge(p->m_pT_Out + i, (secp256k1_ge*) (pWrk->m_pGej + i));
#endif // BeamCrypto_ExternalGej
			secp256k1_sha256_write_CompactPoint(&oracle.m_sha, p->m_pT_Out + i);
		}

		// last challenge
		secp256k1_scalar xChallenge;
		Oracle_NextScalar(&oracle, &xChallenge);

		// m_TauX = tau2*x^2 + tau1*x + sk*z^2
		wrap_scalar_mul(pK, pK, &xChallenge); // tau1*x
		wrap_scalar_mul(&xChallenge, &xChallenge, &xChallenge); // x^2
		wrap_scalar_mul(pK + 1, pK + 1, &xChallenge); // tau2*x^2

		wrap_scalar_mul(&zChallenge, &zChallenge, &zChallenge); // z^2

		wrap_scalar_mul(p->m_pTauX, &pWrk->m_sk, &zChallenge); // sk*z^2
		secp256k1_scalar_add(p->m_pTauX, p->m_pTauX, pK);
		secp256k1_scalar_add(p->m_pTauX, p->m_pTauX, pK + 1);

		Suffer(10); // 5 scalar muls
	}

	SECURE_ERASE_OBJ(pWrk->m_sk);
	SECURE_ERASE_OBJ(pK); // tau1/2
	//SECURE_ERASE_OBJ(hv); - no need, last value is the challenge

	return ok;
}

__stack_hungry__
int RangeProof_Calculate(RangeProof* p)
{
	RangeProof_Worker wrk;
	Gej_Init(wrk.m_pGej);
	Gej_Init(wrk.m_pGej + 1);

	CoinID_getSkComm(p->m_pKdf, &p->m_Cid, &wrk.m_sk, &wrk.m_Commitment);

	RangeProof_Calculate_Before_S(p, &wrk);
	RangeProof_Calculate_S(p, &wrk);

	int res = RangeProof_Calculate_After_S(p, &wrk);

	Gej_Destroy(wrk.m_pGej + 1);
	Gej_Destroy(wrk.m_pGej);

	return res;
}

typedef struct
{
	// All the challenges
	secp256k1_scalar x, y, z, zz;
	secp256k1_scalar m_pE[2][6];

	CompactPoint m_A; // needed during verification
	UintBig m_Mu;
	UintBig m_pCondensed[2];

} RangeProof_Repacked;

typedef struct
{
	NonceGenerator m_Ng;

	const RangeProof_Repacked* m_pRepack;

	// in
	UintBig m_Seed;
	uint32_t m_nUser; // has to be no bigger than c_ECC_nBytes - sizeof(Amount). If less - zero-padding is verified
	// out
	void* m_pUser;
	Amount m_Amount;

	secp256k1_scalar m_Sk;
	secp256k1_scalar m_pExtra[2];

} RangeProof_Recovery_Context;

__stack_hungry__
static void RangeProof_Recover_Init(const RangeProof_Packed* pRangeproof, Oracle* pOracle, RangeProof_Recovery_Context* pCtx, RangeProof_Repacked* pRep)
{
	// oracle << p1.A << p1.S
	// oracle >> y, z
	// oracle << p2.T1 << p2.T2
	// oracle >> x
	secp256k1_sha256_write_CompactPointEx(&pOracle->m_sha, &pRangeproof->m_Ax, pRangeproof->m_pYs[1] >> 4);
	secp256k1_sha256_write_CompactPointEx(&pOracle->m_sha, &pRangeproof->m_Sx, pRangeproof->m_pYs[1] >> 5);
	Oracle_NextScalar(pOracle, &pRep->y);
	Oracle_NextScalar(pOracle, &pRep->z);
	secp256k1_sha256_write_CompactPointEx(&pOracle->m_sha, &pRangeproof->m_T1x, pRangeproof->m_pYs[1] >> 6);
	secp256k1_sha256_write_CompactPointEx(&pOracle->m_sha, &pRangeproof->m_T2x, pRangeproof->m_pYs[1] >> 7);
	Oracle_NextScalar(pOracle, &pRep->x);

	wrap_scalar_mul(&pRep->zz, &pRep->z, &pRep->z); // z^2


	static_assert(sizeof(pRangeproof->m_pLRx) == sizeof(pRep->m_pE), "");

	secp256k1_sha256_write_UintBig(&pOracle->m_sha, &pRangeproof->m_tDot);
	Oracle_NextScalar(pOracle, pRep->m_pE[0]); // dot-multiplier, unneeded atm

	for (uint32_t iCycle = 0; iCycle < _countof(pRangeproof->m_pLRx); iCycle++)
	{
		Oracle_NextScalar(pOracle, pRep->m_pE[0] + iCycle); // challenge
		wrap_scalar_inverse(pRep->m_pE[1] + iCycle, pRep->m_pE[0] + iCycle);

		for (uint32_t j = 0; j < 2; j++)
		{
			uint32_t iBit = (iCycle << 1) + j;
			secp256k1_sha256_write_CompactPointEx(&pOracle->m_sha, pRangeproof->m_pLRx[iCycle] + j, pRangeproof->m_pYs[iBit >> 3] >> (7 & iBit));
		}
	}

	int overflow;
	secp256k1_scalar_set_b32(&pCtx->m_Sk, pRangeproof->m_Taux.m_pVal, &overflow);

	pRep->m_A.m_X = pRangeproof->m_Ax;
	pRep->m_A.m_Y = (1 & (pRangeproof->m_pYs[1] >> 4));
	pRep->m_Mu = pRangeproof->m_Mu;
	memcpy(pRep->m_pCondensed, pRangeproof->m_pCondensed, sizeof(pRangeproof->m_pCondensed));
}


__stack_hungry__
static int RangeProof_Recover1(RangeProof_Recovery_Context* pCtx)
{
	static const char szSalt[] = "bulletproof";
	NonceGenerator_Init(&pCtx->m_Ng, szSalt, sizeof(szSalt), &pCtx->m_Seed);

	secp256k1_scalar alpha_minus_params, ro, tmp;
	NonceGenerator_NextScalar(&pCtx->m_Ng, &alpha_minus_params);
	NonceGenerator_NextScalar(&pCtx->m_Ng, &ro);

	const RangeProof_Repacked* const pRep = pCtx->m_pRepack;

	// m_Mu = alpha + ro*x
	// alpha = m_Mu - ro*x = alpha_minus_params + params
	// params = m_Mu - ro*x - alpha_minus_params
	wrap_scalar_mul(&ro, &ro, &pRep->x);
	secp256k1_scalar_add(&tmp, &alpha_minus_params, &ro);
	secp256k1_scalar_negate(&tmp, &tmp); // - ro*x - alpha_minus_params

	int overflow;
	secp256k1_scalar_set_b32(&ro, pRep->m_Mu.m_pVal, &overflow);
	if (overflow)
		return 0;

	secp256k1_scalar_add(&tmp, &tmp, &ro);

	{
		static_assert(sizeof(ro) >= c_ECC_nBytes, "");
		uint8_t* pBlob = (uint8_t*) &ro; // just reuse this mem

		secp256k1_scalar_get_b32(pBlob, &tmp);

		assert(pCtx->m_nUser <= c_ECC_nBytes - sizeof(Amount));
		uint32_t nPad = c_ECC_nBytes - sizeof(Amount) - pCtx->m_nUser;

		if (!memis0(pBlob, nPad))
			return 0;

		memcpy(pCtx->m_pUser, pBlob + nPad, pCtx->m_nUser);

		// recover value. It's always at the buf end
		pCtx->m_Amount = bswap64_be(*(Amount*)(pBlob + c_ECC_nBytes - sizeof(Amount)));
	}

	secp256k1_scalar_add(&alpha_minus_params, &alpha_minus_params, &tmp); // just alpha

	// Recalculate p1.A, make sure we get the correct result
	union {
		gej_t comm;
		CompactPoint pt;
	} u;

	secp256k1_ge ge;

	Gej_Init(&u.comm);
	MulG(&u.comm, &alpha_minus_params);
	RangeProof_Calculate_A_Bits(&u.comm, &ge, pCtx->m_Amount);

	Point_Ge_from_Gej(&ge, &u.comm);
	Gej_Destroy(&u.comm);

	Point_Compact_from_Ge(&u.pt, &ge);

	if (memcmp(&u.pt, &pRep->m_A, sizeof(u.pt)))
		return 0; // false positive

	return 1;
}

__stack_hungry__
static void RangeProof_Recover2(RangeProof_Recovery_Context* pCtx)
{
	const RangeProof_Repacked* const pRep = pCtx->m_pRepack;

	// Assuming pCtx->m_Sk initialized to tauX

	secp256k1_scalar tau1, tau2;

	// recover the blinding factor
	{
		static const char szSaltSk[] = "bp-key";
		NonceGenerator ngSk;
		NonceGenerator_Init(&ngSk, szSaltSk, sizeof(szSaltSk), &pCtx->m_Seed);
		NonceGenerator_NextScalar(&ngSk, &tau1);
		NonceGenerator_NextScalar(&ngSk, &tau2);
	}

	wrap_scalar_mul(&tau2, &tau2, &pRep->x);
	secp256k1_scalar_add(&tau2, &tau2, &tau1);
	wrap_scalar_mul(&tau2, &tau2, &pRep->x); // tau2*x^2 + tau1*x

	secp256k1_scalar_negate(&tau2, &tau2);
	secp256k1_scalar_add(&pCtx->m_Sk, &pCtx->m_Sk, &tau2);

	wrap_scalar_inverse(&tau2, &pRep->zz); // heavy operation
	wrap_scalar_mul(&pCtx->m_Sk, &pCtx->m_Sk, &tau2);
}

__stack_hungry__
static void RangeProof_Recover3(RangeProof_Recovery_Context* pCtx)
{
	const RangeProof_Repacked* const pRep = pCtx->m_pRepack;


	secp256k1_scalar z_1, two, zzTwoPwr;

	secp256k1_scalar yPwr;
	secp256k1_scalar_set_int(&yPwr, 1);
	secp256k1_scalar_set_int(&two, 2);

	secp256k1_scalar_negate(&z_1, &yPwr);
	secp256k1_scalar_add(&z_1, &z_1, &pRep->z);
	const secp256k1_scalar* pZ[] = { &pRep->z, &z_1 }; // z, z-1

	zzTwoPwr = pRep->zz;

	static_assert(!(nDims & 1), ""); // must be even

	for (unsigned int j = 0; j < 2; j++)
	{
		secp256k1_scalar pS[nDims / 2]; // 32 elements, 1K stack size. Perform 1st condensation in-place (otherwise we'd need to prepare 64 elements first)

		for (uint32_t i = 0; i < nDims; i++)
		{
			secp256k1_scalar val;
			NonceGenerator_NextScalar(&pCtx->m_Ng, &val);

			uint32_t bit = 1 & (pCtx->m_Amount >> i);
			secp256k1_scalar tmp2;

			if (j)
			{
				wrap_scalar_mul(&val, &val, &pRep->x); // pS[i] *= x;
				wrap_scalar_mul(&val, &val, &yPwr); // pS[i] *= yPwr;

				wrap_scalar_mul(&tmp2, pZ[!bit], &yPwr);
				secp256k1_scalar_add(&tmp2, &tmp2, &zzTwoPwr);
				secp256k1_scalar_add(&val, &val, &tmp2); // pS[i] += pZ[!bit]*yPwr + z^2*2^i

				wrap_scalar_mul(&zzTwoPwr, &zzTwoPwr, &two); // x2
				wrap_scalar_mul(&yPwr, &yPwr, &pRep->y);
			}
			else
			{
				wrap_scalar_mul(&val, &val, &pRep->x); // pS[i] *= x;

				secp256k1_scalar_negate(&tmp2, pZ[bit]);
				secp256k1_scalar_add(&val, &val, &tmp2); // pS[i] -= pZ[bit];
			}

			// 1st condensation in-place
			if (i < nDims / 2)
				wrap_scalar_mul(pS + i, &val, pRep->m_pE[j]);
			else
			{
				wrap_scalar_mul(&val, &val, pRep->m_pE[!j]);
				secp256k1_scalar_add(pS + i - nDims / 2, pS + i - nDims / 2, &val);
			}
		}

		// all other condensation cycles
		uint32_t nStep = nDims / 2;
		for (uint32_t iCycle = 1; iCycle < 6; iCycle++)
		{
			nStep >>= 1;
			assert(nStep);

			for (uint32_t i = 0; i < nStep; i++)
			{
				wrap_scalar_mul(pS + i, pS + i, pRep->m_pE[j] + iCycle);
				wrap_scalar_mul(pS + nStep + i, pS + nStep + i, pRep->m_pE[!j] + iCycle);
				secp256k1_scalar_add(pS + i, pS + i, pS + nStep + i);
			}

		}
		assert(1 == nStep);

		int overflow;
		secp256k1_scalar_set_b32(pS + 1, pRep->m_pCondensed[j].m_pVal, &overflow);
		secp256k1_scalar_negate(pS, pS);
		secp256k1_scalar_add(pS, pS, pS + 1); // the difference

		// now let's estimate the difference that would be if extra == 1.
		pS[1] = pRep->x;
		for (uint32_t iCycle = 0; iCycle < 6; iCycle++)
			wrap_scalar_mul(pS + 1, pS + 1, pRep->m_pE[j] + iCycle);

		wrap_scalar_inverse(pCtx->m_pExtra + j, pS + 1);
		wrap_scalar_mul(pCtx->m_pExtra + j, pCtx->m_pExtra + j, pS);
	}

}

//////////////////////////////
// Signature
__stack_hungry__
void Signature_GetChallengeEx(const CompactPoint* pNoncePub, const UintBig* pMsg, secp256k1_scalar* pE)
{
	Oracle oracle;
	Oracle_Init(&oracle);
	secp256k1_sha256_write_CompactPoint(&oracle.m_sha, pNoncePub);
	secp256k1_sha256_write_UintBig(&oracle.m_sha, pMsg);

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
	union {
		secp256k1_hmac_sha256_t hmac;
		gej_t gej;
	} u2;

	NonceGenerator ng;
	static const char szSalt[] = "beam-Schnorr";
	NonceGenerator_InitBegin(&ng, &u2.hmac, szSalt, sizeof(szSalt));

	union
	{
		UintBig sk;
		secp256k1_scalar nonce;
	} u;

	static_assert(sizeof(u.nonce) >= sizeof(u.sk), ""); // means nonce completely overwrites the sk

	secp256k1_scalar_get_b32(u.sk.m_pVal, pSk);
	secp256k1_hmac_sha256_write_UintBig(&u2.hmac, &u.sk);
	secp256k1_hmac_sha256_write_UintBig(&u2.hmac, pMsg);

	NonceGenerator_InitEnd(&ng, &u2.hmac);
	NonceGenerator_NextScalar(&ng, &u.nonce);
	SECURE_ERASE_OBJ(ng);

	// expose the nonce
	Gej_Init(&u2.gej);
	MulG(&u2.gej, &u.nonce);

	Point_Compact_from_Gej(&p->m_NoncePub, &u2.gej);
	Gej_Destroy(&u2.gej);

	Signature_SignPartial(p, pMsg, pSk, &u.nonce);

	SECURE_ERASE_OBJ(u.nonce);
}

__stack_hungry__
void Signature_SignPartialEx(UintBig* pRes, const secp256k1_scalar* pE, const secp256k1_scalar* pSk, const secp256k1_scalar* pNonce)
{
	secp256k1_scalar k;
	wrap_scalar_mul(&k, pE, pSk);
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
static int Signature_IsValid_Internal(const Signature* p, const UintBig* pMsg, CustomGenerator* pPkGen)
{
	gej_t gej;
	Gej_Init(&gej);

	union
	{
		struct {
			secp256k1_scalar k;
			secp256k1_scalar s;
			int overflow;
			MultiMac_WNaf wnaf;
		} p1;

		secp256k1_ge geNonce;
	} u;

	// for historical reasons we don't check for overflow, i.e. theoretically there can be an ambiguity, but it makes not much sense for the attacker
	secp256k1_scalar_set_b32(&u.p1.k, p->m_k.m_pVal, &u.p1.overflow);

#ifdef BeamCrypto_ExternalGej
	MulG(&gej, &u.p1.k);
#else // BeamCrypto_ExternalGej
	MultiMac_Context ctx;
	ctx.m_pRes = &gej;
	ctx.m_Secure.m_Count = 1;
	ctx.m_Secure.m_pGen = Context_get()->m_pGenGJ;
	ctx.m_Secure.m_pK = &u.p1.k;

#endif // BeamCrypto_ExternalGej

	if (!pPkGen)
	{
		// unlikely, but allowed for historical reasons
#ifndef BeamCrypto_ExternalGej
		ctx.m_Fast.m_Count = 0;
#endif // BeamCrypto_ExternalGej
	}
	else
	{
		Signature_GetChallenge(p, pMsg, &u.p1.s);

#ifdef BeamCrypto_ExternalGej
		Gej_MulFast(pPkGen, pPkGen, &u.p1.s);
		Gej_Add(&gej, &gej, pPkGen);
#else // BeamCrypto_ExternalGej
		ctx.m_Fast.m_Count = 1;
		ctx.m_Fast.m_pZDenom = &pPkGen->m_zDenom;
		ctx.m_Fast.m_pGen0 = pPkGen->m_pPt;
		ctx.m_Fast.m_WndBits = c_MultiMac_nBits_Custom;
		ctx.m_Fast.m_pK = &u.p1.s;
		ctx.m_Fast.m_pWnaf = &u.p1.wnaf;
#endif // BeamCrypto_ExternalGej
	}

#ifndef BeamCrypto_ExternalGej
	MultiMac_Calculate(&ctx);
#endif // BeamCrypto_ExternalGej

	int res = Point_Ge_from_Compact(&u.geNonce, &p->m_NoncePub);
	if (res)
	{
		wrap_gej_add_ge_var(&gej, &gej, &u.geNonce);
		res = Gej_Is_infinity(&gej);
	}

	Gej_Destroy(&gej);
	return res;
}

__stack_hungry__
int Signature_IsValid(const Signature* p, const UintBig* pMsg, const CompactPoint* pPk)
{
	CustomGenerator gen; // very large

#ifdef BeamCrypto_ExternalGej
	secp256k1_ge ge;
	secp256k1_ge* const pGe = &ge;
#else // BeamCrypto_ExternalGej

	static_assert(sizeof(gen) >= sizeof(secp256k1_ge), "");
	secp256k1_ge* const pGe = (secp256k1_ge*)&gen;
#endif // BeamCrypto_ExternalGej

	if (!Point_Ge_from_Compact(pGe, pPk))
		return 0; // bad Pubkey

	if (secp256k1_ge_is_infinity(pGe))
		return Signature_IsValid_Internal(p, pMsg, 0);

#ifdef BeamCrypto_ExternalGej
	Gej_Init(&gen);
	Point_Gej_from_Ge(&gen, pGe);
#else // BeamCrypto_ExternalGej
	MultiMac_Fast_Custom_Init(&gen, pGe);
#endif // #ifdef BeamCrypto_ExternalGej

	int res = Signature_IsValid_Internal(p, pMsg, &gen);

#ifdef BeamCrypto_ExternalGej
	Gej_Destroy(&gen);
#endif // BeamCrypto_ExternalGej

	return res;
}

__stack_hungry__
static int Signature_IsValid_Ex(const Signature* p, const UintBig* pMsg, const UintBig* pPeer)
{
	CompactPoint pt;
	pt.m_X = *pPeer;
	pt.m_Y = 0;

	return Signature_IsValid(p, pMsg, &pt);
}


//////////////////////////////
// TxKernel
__stack_hungry__
void TxKernel_getID_Ex(const TxKernelUser* pUser, const TxKernelCommitments* pComms, UintBig* pMsg, const UintBig* pNestedIDs, uint32_t nNestedIDs)
{
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);

	secp256k1_sha256_write_Num(&sha, pUser->m_Fee);
	secp256k1_sha256_write_Num(&sha, pUser->m_hMin);
	secp256k1_sha256_write_Num(&sha, pUser->m_hMax);

	secp256k1_sha256_write_CompactPoint(&sha, &pComms->m_Commitment);
	secp256k1_sha256_write_Num(&sha, 0); // former m_AssetEmission

	secp256k1_sha256_write_u8(&sha, 0); // extended flags, irrelevent for HW wallet

	for (uint32_t i = 0; i < nNestedIDs; i++)
	{
		secp256k1_sha256_write_u8(&sha, 0);
		secp256k1_sha256_write_UintBig(&sha, pNestedIDs + i);
	}

	secp256k1_sha256_write_u8(&sha, 1); // no more nested kernels

	secp256k1_sha256_finalize(&sha, pMsg->m_pVal);
}

void TxKernel_getID(const TxKernelUser* pUser, const TxKernelCommitments* pComms, UintBig* pMsg)
{
	TxKernel_getID_Ex(pUser, pComms, pMsg, 0, 0);
}

__stack_hungry__
int TxKernel_IsValid(const TxKernelUser* pUser, const TxKernelCommitments* pComms, const UintBig* pSig)
{
	UintBig msg;
	TxKernel_getID(pUser, pComms, &msg);

	Signature sig;
	sig.m_NoncePub = pComms->m_NoncePub;
	sig.m_k = *pSig;


	return Signature_IsValid(&sig, &msg, &pComms->m_Commitment);
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
	secp256k1_sha256_write_UintBig(pSha, &hv);
	secp256k1_sha256_write_u8(pSha, 1);
	secp256k1_sha256_write_Num(pSha, nType);
	secp256k1_sha256_write_u8(pSha, 1); // nested break
}

//////////////////////////////
// KeyKeeper - pub Kdf export
__stack_hungry__
static void Kdf2Pub(const Kdf* pKdf, KdfPub* pRes)
{
	pRes->m_Secret = pKdf->m_Secret;

	gej_t pGej[2];
	Gej_Init(pGej);
	Gej_Init(pGej + 1);

	MulG(pGej, &pKdf->m_kCoFactor);
	MulJ(pGej + 1, &pKdf->m_kCoFactor);

#ifdef BeamCrypto_ExternalGej
	Point_Compact_from_Gej(&pRes->m_CoFactorG, pGej);
	Point_Compact_from_Gej(&pRes->m_CoFactorJ, pGej + 1);
#else // #ifdef BeamCrypto_ExternalGej

	Point_Gej_2_Normalize(pGej);

	Point_Compact_from_Ge(&pRes->m_CoFactorG, (secp256k1_ge*) pGej);
	Point_Compact_from_Ge(&pRes->m_CoFactorJ, (secp256k1_ge*) (pGej + 1));

#endif // #ifdef BeamCrypto_ExternalGej

	Gej_Destroy(pGej + 1);
	Gej_Destroy(pGej);
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
#define PROTO_METHOD(name) __stack_hungry__ static uint16_t HandleProto_##name(KeyKeeper* const p, OpIn_##name* const pIn, uint32_t nIn, OpOut_##name* const pOut, uint32_t nOut, uint32_t* pOutSize)

#define PROTO_UNUSED_ARGS \
	UNUSED(p); \
	UNUSED(pIn); \
	UNUSED(nIn); \
	UNUSED(pOut); \
	UNUSED(nOut);  \
	UNUSED(pOutSize);

#pragma pack (push, 1)
#define THE_MACRO_Field(type, name) type m_##name;
#define THE_MACRO_OpCode(id, name) \
typedef struct { \
	uint8_t m_OpCode; \
	BeamCrypto_ProtoRequest_##name(THE_MACRO_Field) \
} OpIn_##name; \
typedef struct { \
	uint8_t m_StatusCode; \
	BeamCrypto_ProtoResponse_##name(THE_MACRO_Field) \
} OpOut_##name; \
PROTO_METHOD(name);

BeamCrypto_ProtoMethods(THE_MACRO_OpCode)

#undef THE_MACRO_OpCode
#undef THE_MACRO_Field

#pragma pack (pop)

__attribute__((noinline))
void memcpy_unaligned(void* pDst, const void* pSrc, uint32_t n)
{
	memcpy(pDst, pSrc, n);
}

#define H2N_uint(n_val_unaligned, h_val, bits) \
do { \
	uint##bits##_t n_val = bswap##bits##_le(h_val); \
	static_assert(sizeof(n_val_unaligned) == sizeof(n_val), ""); \
	memcpy_unaligned(&n_val_unaligned, &n_val, sizeof(n_val)); \
} while(0)

#define N2H_uint_inplace(val, bits) \
do { \
	static_assert(sizeof(val) == sizeof(uint##bits##_t), ""); \
	val = bswap##bits##_le(val); \
} while(0)

#define N2H_uint(h_val, n_val_unaligned, bits) \
do { \
	static_assert(sizeof(n_val_unaligned) == sizeof(h_val), ""); \
	memcpy_unaligned(&h_val, &n_val_unaligned, sizeof(h_val)); \
	N2H_uint_inplace(h_val, bits); \
} while(0)

void N2H_CoinID(CoinID* p, const CoinID* p_unaligned)
{
	memcpy_unaligned(p, p_unaligned, sizeof(*p));

	N2H_uint_inplace(p->m_Amount, 64);
	N2H_uint_inplace(p->m_Idx, 64);
	N2H_uint_inplace(p->m_AssetID, 32);
	N2H_uint_inplace(p->m_SubIdx, 32);
	N2H_uint_inplace(p->m_Type, 32);
}

void N2H_ShieldedInput_Fmt(ShieldedInput_Fmt* p, const ShieldedInput_Fmt* p_unaligned)
{
	memcpy_unaligned(p, p_unaligned, sizeof(*p));

	N2H_uint_inplace(p->m_Fee, 64);
	N2H_uint_inplace(p->m_Amount, 64);
	N2H_uint_inplace(p->m_AssetID, 32);
	N2H_uint_inplace(p->m_nViewerIdx, 32);
}

uint16_t MakeStatus(uint8_t major, uint8_t minor)
{
	return (((uint16_t) minor) << 8) | major;
}

uint16_t KeyKeeper_Invoke(KeyKeeper* p, const uint8_t* pIn, uint32_t nIn, uint8_t* pOut, uint32_t* pOutSize)
{
	if (!nIn)
		return MakeStatus(c_KeyKeeper_Status_ProtoError, 0xfd);

	uint32_t nOutSize = *pOutSize;

	switch (*pIn)
	{
#define THE_MACRO(id, name) \
	case id: \
	{ \
		if ((nIn < sizeof(OpIn_##name)) || (nOutSize < sizeof(OpOut_##name))) \
			return MakeStatus(c_KeyKeeper_Status_ProtoError, 0xfe); \
 \
		*pOutSize = sizeof(OpOut_##name); \
\
		return HandleProto_##name(p, (OpIn_##name*) pIn, nIn - sizeof(OpIn_##name), (OpOut_##name*) pOut, nOutSize - sizeof(OpOut_##name), pOutSize); \
 	} \
	break; \

		BeamCrypto_ProtoMethods(THE_MACRO)
#undef THE_MACRO

	}

	return MakeStatus(c_KeyKeeper_Status_ProtoError, 0xff);
}

PROTO_METHOD(Version)
{
	PROTO_UNUSED_ARGS;

	if (nIn)
		return c_KeyKeeper_Status_ProtoError; // size mismatch

	static_assert(sizeof(pOut->m_Signature) == sizeof(BeamCrypto_Signature) - sizeof(char), "");
	memcpy(pOut->m_Signature, BeamCrypto_Signature, sizeof(BeamCrypto_Signature) - sizeof(char));

	H2N_uint(pOut->m_Version, BeamCrypto_CurrentVersion, 32);
	H2N_uint(pOut->m_Flags, 0, 32);

	return c_KeyKeeper_Status_Ok;
}

PROTO_METHOD(GetNumSlots)
{
	PROTO_UNUSED_ARGS;

	if (nIn)
		return c_KeyKeeper_Status_ProtoError; // size mismatch

	H2N_uint(pOut->m_Value, KeyKeeper_getNumSlots(p), 32);


	return c_KeyKeeper_Status_Ok;
}

PROTO_METHOD(GetPKdf)
{
	PROTO_UNUSED_ARGS;

	if (nIn)
		return c_KeyKeeper_Status_ProtoError; // size mismatch

	uint32_t iChild = (uint32_t) -1;
	KeyKeeper_GetPKdf(p, &pOut->m_Value, pIn->m_Kind ? &iChild : 0);

	return c_KeyKeeper_Status_Ok;
}

PROTO_METHOD(GetImage)
{
	PROTO_UNUSED_ARGS;

	if (nIn)
		return c_KeyKeeper_Status_ProtoError; // size mismatch

	uint32_t iChild;
	N2H_uint(iChild, pIn->m_iChild, 32);

	Kdf kdfC;
	Kdf_getChild(&kdfC, iChild, &p->m_MasterKey);

	secp256k1_scalar sk;
	Kdf_Derive_SKey(&kdfC, &pIn->m_hvSrc, &sk);
	SECURE_ERASE_OBJ(kdfC);

	const uint8_t pFlag[] = {
		pIn->m_bG, // copy, coz it'd be overwritten by the result
		pIn->m_bJ
	};
	gej_t pGej[_countof(pFlag)];

	unsigned int nCount = 0;

	for (unsigned int i = 0; i < _countof(pGej); i++)
	{
		if (!pFlag[i])
			continue;

		Gej_Init(pGej + i);
		MulPoint(pGej + i, Context_get()->m_pGenGJ + i, &sk);
		nCount++;
	}

	if (!nCount)
		return c_KeyKeeper_Status_Unspecified;

#ifndef BeamCrypto_ExternalGej
	if (_countof(pGej) == nCount)
		Point_Gej_2_Normalize(pGej);
#endif // BeamCrypto_ExternalGej

	CompactPoint* pRes = &pOut->m_ptImageG;

	for (unsigned int i = 0; i < _countof(pGej); i++)
	{
		if (pFlag[i])
		{
#ifndef BeamCrypto_ExternalGej
			if (_countof(pGej) == nCount)
				Point_Compact_from_Ge(pRes + i, (secp256k1_ge*)(pGej + i));
			else
#endif // BeamCrypto_ExternalGej
				Point_Compact_from_Gej(pRes + i, pGej + i);

			Gej_Destroy(pGej + i);
		}
		else
			ZERO_OBJ(pRes[i]);
	}

	return c_KeyKeeper_Status_Ok;
}

PROTO_METHOD(CreateOutput)
{
	PROTO_UNUSED_ARGS;

	if (nIn)
		return c_KeyKeeper_Status_ProtoError; // size mismatch

	RangeProof ctx;
	N2H_CoinID(&ctx.m_Cid, &pIn->m_Cid);
	ctx.m_pKdf = &p->m_MasterKey;
	ctx.m_pT_In = pIn->m_pT;
	ctx.m_pT_Out = pIn->m_pT; // use same buf (since we changed to in/out buf design). Copy res later

	secp256k1_scalar sBuf;
	ctx.m_pTauX = &sBuf;

	static_assert(sizeof(UintBig) == sizeof(secp256k1_scalar), "");

	if (memis0(pIn->m_pKExtra->m_pVal, sizeof(pIn->m_pKExtra)))
		ctx.m_pKExtra = 0;
	else
		ctx.m_pKExtra = pIn->m_pKExtra;

	ctx.m_pAssetGen = IsUintBigZero(&pIn->m_ptAssetGen.m_X) ? 0 : &pIn->m_ptAssetGen;

	if (!RangeProof_Calculate(&ctx))
		return c_KeyKeeper_Status_Unspecified;

	// copy into out. Do it carefully, since in/out share the same mem
	static_assert(sizeof(pOut->m_pT) == sizeof(pIn->m_pT), "");
	memmove(pOut->m_pT, pIn->m_pT, sizeof(pOut->m_pT)); // MUST use memmove!

	secp256k1_scalar_get_b32(pOut->m_TauX.m_pVal, &sBuf);

	return c_KeyKeeper_Status_Ok;
}

//////////////////////////////
// KeyKeeper - transaction common. Aggregation
static int TxAggr_AddAmount_Raw(int64_t* pRcv, Amount newVal, int isOut)
{
	// beware of overflow
	int64_t val0 = *pRcv;

	if (isOut)
	{
		*pRcv += newVal;
		if (*pRcv < val0)
			return 0;
	}
	else
	{
		*pRcv -= newVal;
		if (*pRcv > val0)
			return 0;
	}

	return 1;
}

static int TxAggr_AddAmount_Uns(Amount* pRes, Amount newVal)
{
	*pRes += newVal;
	return (*pRes) >= newVal;
}

static int TxAggr_AddAmount(KeyKeeper* p, Amount newVal, AssetID aid, int isOut)
{
	int64_t* pRcv = &p->u.m_TxBalance.m_RcvBeam;

	if (aid)
	{
		if (p->u.m_TxBalance.m_Aid)
		{
			if (p->u.m_TxBalance.m_Aid != aid)
				return 0; // only 1 additional asset type is supported in a tx
		}
		else
			p->u.m_TxBalance.m_Aid = aid;

		pRcv = &p->u.m_TxBalance.m_RcvAsset;
	}

	return TxAggr_AddAmount_Raw(pRcv, newVal, isOut);
}

__stack_hungry__
static uint16_t TxAggr_AddCoins(KeyKeeper* p, CoinID* pCid_unaligned, uint32_t nCount, int isOut)
{
	for (uint32_t i = 0; i < nCount; i++)
	{
		CoinID cid;
		N2H_CoinID(&cid, pCid_unaligned + i);

		if (!TxAggr_AddAmount(p, cid.m_Amount, cid.m_AssetID, isOut))
			return MakeStatus(c_KeyKeeper_Status_Unspecified, 1);

		secp256k1_scalar sk;
		CoinID_getSkComm(&p->m_MasterKey, &cid, &sk, 0);

		if (!isOut)
			secp256k1_scalar_negate(&sk, &sk);

		secp256k1_scalar_add(&p->u.m_TxBalance.m_sk, &p->u.m_TxBalance.m_sk, &sk);
		SECURE_ERASE_OBJ(sk);
	}

	return c_KeyKeeper_Status_Ok;
}

__stack_hungry__
static uint16_t TxAggr_AddShieldedInputs(KeyKeeper* p, uint8_t* pIns_unaligned, uint32_t nCount)
{
	for (uint32_t i = 0; i < nCount; i++)
	{
		const ShieldedInput_Blob* pBlob = (const ShieldedInput_Blob*) pIns_unaligned;
		pIns_unaligned += sizeof(ShieldedInput_Blob);

		ShieldedInput_Fmt fmt;
		N2H_ShieldedInput_Fmt(&fmt, (const ShieldedInput_Fmt*) pIns_unaligned);
		pIns_unaligned += sizeof(ShieldedInput_Fmt);

		if (!TxAggr_AddAmount(p, fmt.m_Amount, fmt.m_AssetID, 0))
			return MakeStatus(c_KeyKeeper_Status_Unspecified, 1);

		if (fmt.m_Fee)
		{
			// Starting from HF3 shielded input fees are optional. And basically should not be used. But currently we support them
			if (!TxAggr_AddAmount_Uns(&p->u.m_TxBalance.m_ImplicitFee, fmt.m_Fee))
				return MakeStatus(c_KeyKeeper_Status_Unspecified, 1); // overflow
		}

		secp256k1_scalar sk;
		ShieldedInput_getSk(p, pBlob, &fmt, &sk);

		secp256k1_scalar_negate(&sk, &sk);
		secp256k1_scalar_add(&p->u.m_TxBalance.m_sk, &p->u.m_TxBalance.m_sk, &sk);
		SECURE_ERASE_OBJ(sk);
	}

	return c_KeyKeeper_Status_Ok;
}

static uint16_t TxAggr_Get(const KeyKeeper* p, TxSummary* pRes, const TxCommonIn* pTx, uint8_t isSender)
{
	if (c_KeyKeeper_State_TxBalance != p->m_State)
		return MakeStatus(c_KeyKeeper_Status_Unspecified, 10);

	memcpy_unaligned(&pRes->m_Krn, &pTx->m_Krn, sizeof(pRes->m_Krn));

	N2H_uint_inplace(pRes->m_Krn.m_Fee, 64);
	N2H_uint_inplace(pRes->m_Krn.m_hMin, 64);
	N2H_uint_inplace(pRes->m_Krn.m_hMax, 64);

	int64_t rcvVal = p->u.m_TxBalance.m_RcvBeam;
	if (isSender)
	{
		Amount totalFee = p->u.m_TxBalance.m_ImplicitFee;
		if (!TxAggr_AddAmount_Uns(&totalFee, pRes->m_Krn.m_Fee))
			return MakeStatus(c_KeyKeeper_Status_Unspecified, 15);

		// we're paying the fee. Subtract it from the net value we're sending
		if (!TxAggr_AddAmount_Raw(&rcvVal, totalFee, 1))
			return MakeStatus(c_KeyKeeper_Status_Unspecified, 17);
	}
	else
	{
		if (p->u.m_TxBalance.m_ImplicitFee)
			// Implicit fees are not allowed for rcv tx (since we don't ask user permission)
			return MakeStatus(c_KeyKeeper_Status_Unspecified, 16);
	}

	if (p->u.m_TxBalance.m_RcvAsset)
	{
		if (rcvVal)
			return MakeStatus(c_KeyKeeper_Status_Unspecified, 11); // nnz net result in both beams and assets.

		rcvVal = p->u.m_TxBalance.m_RcvAsset;
		pRes->m_Aid = p->u.m_TxBalance.m_Aid;
	}
	else
		pRes->m_Aid = 0;

	if (isSender)
	{
		if (rcvVal > 0)
			return MakeStatus(c_KeyKeeper_Status_Unspecified, 12); // actually receiving

		pRes->m_NetAmount = -rcvVal;
	}
	else
	{
		if (rcvVal <= 0)
			return MakeStatus(c_KeyKeeper_Status_Unspecified, 13); // not receiving
		pRes->m_NetAmount = rcvVal;
	}

	return c_KeyKeeper_Status_Ok;
}

static void TxAggr_ToOffsetEx(const KeyKeeper* p, const secp256k1_scalar* pKrn, UintBig* pOffs)
{
	secp256k1_scalar kOffs;
	secp256k1_scalar_add(&kOffs, &p->u.m_TxBalance.m_sk, pKrn);
	secp256k1_scalar_negate(&kOffs, &kOffs);

	secp256k1_scalar_get_b32(pOffs->m_pVal, &kOffs);
	// kOffs isn't secret
}

static void TxAggr_ToOffset(const KeyKeeper* p, const secp256k1_scalar* pKrn, TxCommonOut* pTx)
{
	TxAggr_ToOffsetEx(p, pKrn, &pTx->m_TxSig.m_kOffset);
}

PROTO_METHOD(TxAddCoins)
{
	PROTO_UNUSED_ARGS;

	if ((pIn->m_Reset) || (c_KeyKeeper_State_TxBalance != p->m_State))
	{
		ZERO_OBJ(p->u);
		p->m_State = c_KeyKeeper_State_TxBalance;
	}

	uint32_t nSize =
		(sizeof(CoinID) * pIn->m_Ins) +
		(sizeof(CoinID) * pIn->m_Outs) +
		((sizeof(ShieldedInput_Blob) + sizeof(ShieldedInput_Fmt)) * pIn->m_InsShielded);

	if (nIn != nSize)
		return c_KeyKeeper_Status_ProtoError;

	CoinID* pCid_unaligned = (CoinID*)(pIn + 1);

	uint16_t errCode = TxAggr_AddCoins(p, pCid_unaligned, pIn->m_Ins, 0);
	if (c_KeyKeeper_Status_Ok != errCode)
		return errCode;

	pCid_unaligned += pIn->m_Ins;

	errCode = TxAggr_AddCoins(p, pCid_unaligned, pIn->m_Outs, 1);
	if (c_KeyKeeper_Status_Ok != errCode)
		return errCode;

	pCid_unaligned += pIn->m_Outs;

	return TxAggr_AddShieldedInputs(p, (uint8_t*) pCid_unaligned, pIn->m_InsShielded);
}

//////////////////////////////
// KeyKeeper - Kernel modification
typedef struct
{
	secp256k1_scalar m_kKrn;
	secp256k1_scalar m_kNonce;
} KernelKeys;

__stack_hungry__
static int KernelUpdateKeysEx(TxKernelCommitments* pComms, const KernelKeys* pKeys, const TxKernelCommitments* pAdd)
{
	gej_t pGej[2];
	Gej_Init(pGej);
	Gej_Init(pGej + 1);

	MulG(pGej, &pKeys->m_kKrn);
	MulG(pGej + 1, &pKeys->m_kNonce);

	if (pAdd)
	{
		secp256k1_ge ge;
		if (!Point_Ge_from_Compact(&ge, &pAdd->m_Commitment))
			return 0;

		wrap_gej_add_ge_var(pGej, pGej, &ge);

		if (!Point_Ge_from_Compact(&ge, &pAdd->m_NoncePub))
			return 0;

		wrap_gej_add_ge_var(pGej + 1, pGej + 1, &ge);
	}

#ifdef BeamCrypto_ExternalGej
	Point_Compact_from_Gej(&pComms->m_Commitment, pGej);
	Point_Compact_from_Gej(&pComms->m_NoncePub, pGej + 1);
#else // BeamCrypto_ExternalGej
	Point_Gej_2_Normalize(pGej);

	Point_Compact_from_Ge(&pComms->m_Commitment, (secp256k1_ge*) pGej);
	Point_Compact_from_Ge(&pComms->m_NoncePub, (secp256k1_ge*) (pGej + 1));
#endif // BeamCrypto_ExternalGej

	Gej_Destroy(pGej);
	Gej_Destroy(pGej + 1);

	return 1;
}

static int KernelUpdateKeys(TxKernelCommitments* pComms, const KernelKeys* pKeys, const TxKernelCommitments* pAdd)
{
	return KernelUpdateKeysEx(pComms, pKeys, pAdd);
}

__stack_hungry__
static void Kernel_SignPartial(UintBig* pSig, const TxKernelCommitments* pComms, const UintBig* pMsg, const KernelKeys* pKeys)
{
	// Note: 1st 2 arguments are unaligned
	secp256k1_scalar e;
	Signature_GetChallengeEx(&pComms->m_NoncePub, pMsg, &e);

	Signature_SignPartialEx(pSig, (secp256k1_scalar*) &e, &pKeys->m_kKrn, &pKeys->m_kNonce);
}


//////////////////////////////
// KeyKeeper - SplitTx
PROTO_METHOD(TxSplit)
{
	PROTO_UNUSED_ARGS;

	if (nIn)
		return c_KeyKeeper_Status_ProtoError;

	TxSummary txs;
	uint16_t errCode = TxAggr_Get(p, &txs, &pIn->m_Tx, 1);
	if (errCode)
		return errCode;

	if (txs.m_NetAmount)
		return MakeStatus(c_KeyKeeper_Status_Unspecified, 21); // not split

	// hash all visible params
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);
	secp256k1_sha256_write_Num(&sha, txs.m_Krn.m_hMin);
	secp256k1_sha256_write_Num(&sha, txs.m_Krn.m_hMax);
	secp256k1_sha256_write_Num(&sha, txs.m_Krn.m_Fee);

	UintBig hv;
	secp256k1_scalar_get_b32(hv.m_pVal, &p->u.m_TxBalance.m_sk);
	secp256k1_sha256_write_UintBig(&sha, &hv);
	secp256k1_sha256_finalize(&sha, hv.m_pVal);

	// derive keys
	static const char szSalt[] = "hw-wlt-split";
	NonceGenerator ng;
	NonceGenerator_Init(&ng, szSalt, sizeof(szSalt), &hv);

	KernelKeys keys;
	NonceGenerator_NextScalar(&ng, &keys.m_kKrn);
	NonceGenerator_NextScalar(&ng, &keys.m_kNonce);
	SECURE_ERASE_OBJ(ng);

	KernelUpdateKeys(&pOut->m_Tx.m_Comms, &keys, 0);

	TxKernel_getID(&txs.m_Krn, &pOut->m_Tx.m_Comms, &hv);

	txs.m_Flags = c_KeyKeeper_ConfirmSpend_Split;
	txs.m_pPeer = 0;
	txs.m_pKrnID = &hv;

	errCode = KeyKeeper_ConfirmSpend(p, &txs);
	if (c_KeyKeeper_Status_Ok != errCode)
		return errCode;

	Kernel_SignPartial(&pOut->m_Tx.m_TxSig.m_kSig, &pOut->m_Tx.m_Comms, &hv, &keys);

	TxAggr_ToOffset(p, &keys.m_kKrn, &pOut->m_Tx);

	SECURE_ERASE_OBJ(keys);

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
	secp256k1_sha256_write_UintBig(&sha, pKernelID);
	secp256k1_sha256_write_UintBig(&sha, pSender);
	secp256k1_sha256_write_Num(&sha, amount);

	if (nAssetID)
	{
		HASH_WRITE_STR(sha, "asset");
		secp256k1_sha256_write_Num(&sha, nAssetID);
	}

	secp256k1_sha256_finalize(&sha, pRes->m_pVal);
}

__stack_hungry__
void DeriveAddress(const KeyKeeper* p, AddrID addrID, secp256k1_scalar* pKey, UintBig* pID)
{
	// derive key
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);
	HASH_WRITE_STR(sha, "kid");

	const uint32_t nType = FOURCC_FROM_STR(tRid);

	secp256k1_sha256_write_Num(&sha, addrID);
	secp256k1_sha256_write_Num(&sha, nType);
	secp256k1_sha256_write_Num(&sha, 0);
	secp256k1_sha256_finalize(&sha, pID->m_pVal);

	Kdf_Derive_SKey(&p->m_MasterKey, pID, pKey);
	Sk2Pk(pID, pKey);
}

//////////////////////////////
// KeyKeeper - ReceiveTx
PROTO_METHOD(TxReceive)
{
	PROTO_UNUSED_ARGS;

	if (nIn)
		return c_KeyKeeper_Status_ProtoError;

	TxSummary txs;
	uint16_t errCode = TxAggr_Get(p, &txs, &pIn->m_Tx, 0);
	if (c_KeyKeeper_Status_Ok != errCode)
		return errCode;

	assert(txs.m_NetAmount);

	TxMutualIn txm;
	memcpy_unaligned(&txm, &pIn->m_Mut, sizeof(txm)); // save it before we generate output
	N2H_uint_inplace(txm.m_AddrID, 64);

	// Hash *ALL* the parameters, make the context unique
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);

	UintBig hv;
	TxKernel_getID(&txs.m_Krn, &pIn->m_Comms, &hv); // not a final ID yet

	secp256k1_sha256_write_UintBig(&sha, &hv);
	secp256k1_sha256_write_CompactPoint(&sha, &pIn->m_Comms.m_NoncePub);

	secp256k1_sha256_write_u8(&sha, 0); // not nonconventional
	secp256k1_sha256_write_UintBig(&sha, &pIn->m_Mut.m_Peer);

	secp256k1_sha256_write_Num(&sha, txm.m_AddrID);

	secp256k1_scalar_get_b32(hv.m_pVal, &p->u.m_TxBalance.m_sk);
	secp256k1_sha256_write_UintBig(&sha, &hv);

	secp256k1_sha256_write_Num(&sha, txs.m_NetAmount); // the value being-received
	secp256k1_sha256_write_Num(&sha, txs.m_Aid);

	secp256k1_sha256_finalize(&sha, hv.m_pVal);

	// derive keys
	static const char szSalt[] = "hw-wlt-rcv";
	NonceGenerator ng;
	NonceGenerator_Init(&ng, szSalt, sizeof(szSalt), &hv);

	KernelKeys keys;
	NonceGenerator_NextScalar(&ng, &keys.m_kKrn);
	NonceGenerator_NextScalar(&ng, &keys.m_kNonce);
	SECURE_ERASE_OBJ(ng);

	if (!KernelUpdateKeys(&pOut->m_Tx.m_Comms, &keys, &pIn->m_Comms)) // safe to call, even though pOut and pIn may overlap
		return MakeStatus(c_KeyKeeper_Status_Unspecified, 22);

	TxKernel_getID(&txs.m_Krn, &pOut->m_Tx.m_Comms, &hv); // final ID
	Kernel_SignPartial(&pOut->m_Tx.m_TxSig.m_kSig, &pOut->m_Tx.m_Comms, &hv, &keys);

	TxAggr_ToOffset(p, &keys.m_kKrn, &pOut->m_Tx);

	if (txm.m_AddrID)
	{
		// sign
		UintBig hvID;
		DeriveAddress(p, txm.m_AddrID, &keys.m_kKrn, &hvID);
		GetPaymentConfirmationMsg(&hvID, &txm.m_Peer, &hv, txs.m_NetAmount, txs.m_Aid);
		Signature_Sign(&pOut->m_PaymentProof, &hvID, &keys.m_kKrn);
	}

	return c_KeyKeeper_Status_Ok;
}

//////////////////////////////
// Base58 encoding

static const char g_pBase58Alphabet[] = {
  '1','2','3','4','5','6','7','8','9','A','B','C','D','E','F','G','H','J','K','L','M','N','P','Q','R','S','T','U','V',
  'W','X','Y','Z','a','b','c','d','e','f','g','h','i','j','k','m','n','o','p','q','r','s','t','u','v','w','x','y','z'
};

uint32_t LongIntDiv(uint32_t* p, uint32_t n, uint32_t div)
{
	assert(div && n && p[0]);

	uint64_t carry = 0;
	for (uint32_t i = 0; ; )
	{
		carry |= p[i];
		p[i] = (uint32_t)(carry / div);
		carry %= div;

		if (++i == n)
			break;
		carry <<= 32;
	}

	return (uint32_t)carry;
}

void Base58_EncodeW(char* szEnc, uint32_t nEnc, uint32_t* pW, uint32_t nW)
{
	char* szPos = szEnc + nEnc;

	const uint32_t ord1 = _countof(g_pBase58Alphabet);
	const uint32_t ord5 = ord1 * ord1 * ord1 * ord1 * ord1;

	while (nW)
	{
		if (pW[0])
		{
			uint32_t resid;

			if ((nW > 1) || (*pW >= ord5))
			{
				resid = LongIntDiv(pW, nW, ord5);
				for (int i = 0; i < 4; i++)
				{
					*(--szPos) = g_pBase58Alphabet[resid % ord1];
					resid /= ord1;
				}
			}
			else
				resid = LongIntDiv(pW, nW, ord1);

			*(--szPos) = g_pBase58Alphabet[resid];
		}
		else
		{
			pW++;
			nW--;
		}
	}

	while (szPos > szEnc)
		*(--szPos) = g_pBase58Alphabet[0];
}

void Base58_Encode(char* szEnc, uint32_t nEnc, uint32_t* pWrk, const uint8_t* p, uint32_t n)
{
	uint32_t nResid = n % sizeof(uint32_t);
	if (nResid)
	{
		pWrk[0] = 0;
		memcpy(((uint8_t*)pWrk) + sizeof(uint32_t) - nResid, p, n);
		n /= sizeof(uint32_t);
		n++;
	}
	else
	{
		memcpy(pWrk, p, n);
		n /= sizeof(uint32_t);
	}

	for (uint32_t i = 0; i < n; i++)
		pWrk[i] = bswap32_be(pWrk[i]);

	Base58_EncodeW(szEnc, nEnc, pWrk, n);
}


//////////////////////////////
// KeyKeeper - DisplayEndpoint
PROTO_METHOD(DisplayEndpoint)
{
	PROTO_UNUSED_ARGS;

	if (nIn)
		return c_KeyKeeper_Status_ProtoError; // size mismatch

	AddrID addrID;
	N2H_uint(addrID, pIn->m_AddrID, 64);

	secp256k1_scalar sk;
	UintBig hvAddr;

	DeriveAddress(p, addrID, &sk, &hvAddr);
	SECURE_ERASE_OBJ(sk);

	KeyKeeper_DisplayEndpoint(p, addrID, &hvAddr);

	return c_KeyKeeper_Status_Ok;
}

void PrintEndpoint(char* sz, const UintBig* pID)
{
	uint32_t pWrk[(c_KeyKeeper_Endpoint_Len + sizeof(uint32_t) - 1) / sizeof(uint32_t)];
	Base58_Encode(sz, c_KeyKeeper_Endpoint_Len, pWrk, pID->m_pVal, sizeof(pID->m_pVal));
}


//////////////////////////////
// KeyKeeper - SendTx
typedef struct
{
	TxSummary m_Txs;
	uint32_t m_iSlot;

	KernelKeys m_Keys;

	UintBig m_hvMyID;
	UintBig m_hvToken;

} TxSendContext;

__stack_hungry__
static void TxSend_DeriveKeys(KeyKeeper* p, const OpIn_TxSend2* pIn, TxSendContext* pCtx)
{
	AddrID addrID;
	N2H_uint(addrID, pIn->m_Mut.m_AddrID, 64);
	DeriveAddress(p, addrID, &pCtx->m_Keys.m_kNonce, &pCtx->m_hvMyID);

	KeyKeeper_ReadSlot(p, pCtx->m_iSlot, &pCtx->m_hvToken);
	Kdf_Derive_SKey(&p->m_MasterKey, &pCtx->m_hvToken, &pCtx->m_Keys.m_kNonce);

	// during negotiation kernel height and commitment are adjusted. We should only commit to the Fee
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);
	secp256k1_sha256_write_Num(&sha, pCtx->m_Txs.m_Krn.m_Fee);
	secp256k1_sha256_write_UintBig(&sha, &pIn->m_Mut.m_Peer);
	secp256k1_sha256_write_UintBig(&sha, &pCtx->m_hvMyID);

	uint8_t nFlag = 0; // not nonconventional
	secp256k1_sha256_write_u8(&sha, nFlag);

	secp256k1_scalar_get_b32(pCtx->m_hvToken.m_pVal, &p->u.m_TxBalance.m_sk);
	secp256k1_sha256_write_UintBig(&sha, &pCtx->m_hvToken);
	secp256k1_sha256_write_Num(&sha, pCtx->m_Txs.m_NetAmount);
	secp256k1_sha256_write_Num(&sha, pCtx->m_Txs.m_Aid);

	secp256k1_scalar_get_b32(pCtx->m_hvToken.m_pVal, &pCtx->m_Keys.m_kNonce);
	secp256k1_sha256_write_UintBig(&sha, &pCtx->m_hvToken);
	secp256k1_sha256_finalize(&sha, pCtx->m_hvToken.m_pVal);

	static const char szSalt[] = "hw-wlt-snd";
	NonceGenerator ng;
	NonceGenerator_Init(&ng, szSalt, sizeof(szSalt), &pCtx->m_hvToken);
	NonceGenerator_NextScalar(&ng, &pCtx->m_Keys.m_kKrn);
	SECURE_ERASE_OBJ(ng);

	// derive tx token
	secp256k1_sha256_initialize(&sha);
	HASH_WRITE_STR(sha, "tx.token");

	secp256k1_scalar_get_b32(pCtx->m_hvToken.m_pVal, &pCtx->m_Keys.m_kKrn);
	secp256k1_sha256_write_UintBig(&sha, &pCtx->m_hvToken);
	secp256k1_sha256_finalize(&sha, pCtx->m_hvToken.m_pVal);

	if (IsUintBigZero(&pCtx->m_hvToken))
		pCtx->m_hvToken.m_pVal[_countof(pCtx->m_hvToken.m_pVal) - 1] = 1;
}

uint16_t TxSend_Prepare(KeyKeeper* p, OpIn_TxSend2* pIn, TxSendContext* pCtx)
{
	uint16_t errCode = TxAggr_Get(p, &pCtx->m_Txs, &pIn->m_Tx, 1);
	if (errCode)
		return errCode;

	if (!pCtx->m_Txs.m_NetAmount)
		return MakeStatus(c_KeyKeeper_Status_Unspecified, 21); // not sending

	if (IsUintBigZero(&pIn->m_Mut.m_Peer))
		return MakeStatus(c_KeyKeeper_Status_UserAbort, 22); // conventional transfers must always be signed

	N2H_uint(pCtx->m_iSlot, pIn->m_iSlot, 32);

	if (pCtx->m_iSlot >= KeyKeeper_getNumSlots(p))
		return MakeStatus(c_KeyKeeper_Status_Unspecified, 23);

	TxSend_DeriveKeys(p, pIn, pCtx);

	pCtx->m_Txs.m_pPeer = &pIn->m_Mut.m_Peer;

	return c_KeyKeeper_Status_Ok;
}

PROTO_METHOD(TxSend1)
{
	PROTO_UNUSED_ARGS;

	if (nIn)
		return c_KeyKeeper_Status_ProtoError;

	TxSendContext ctx;
	uint16_t errCode = TxSend_Prepare(p, (OpIn_TxSend2*) pIn, &ctx);
	if (errCode)
		return errCode;

	ctx.m_Txs.m_Flags = 0;
	ctx.m_Txs.m_pKrnID = 0;

	errCode = KeyKeeper_ConfirmSpend(p, &ctx.m_Txs);
	if (c_KeyKeeper_Status_Ok != errCode)
		return errCode;

	pOut->m_UserAgreement = ctx.m_hvToken;

	KernelUpdateKeysEx(&pOut->m_Comms, &ctx.m_Keys, 0);

	return c_KeyKeeper_Status_Ok;
}

PROTO_METHOD(TxSend2)
{
	PROTO_UNUSED_ARGS;

	if (nIn)
		return c_KeyKeeper_Status_ProtoError;

	TxSendContext ctx;
	uint16_t errCode = TxSend_Prepare(p, (OpIn_TxSend2*)pIn, &ctx);
	if (errCode)
		return errCode;

	if (memcmp(pIn->m_UserAgreement.m_pVal, ctx.m_hvToken.m_pVal, sizeof(ctx.m_hvToken.m_pVal)))
		return MakeStatus(c_KeyKeeper_Status_Unspecified, 24); // incorrect user agreement token

	TxKernel_getID(&ctx.m_Txs.m_Krn, &pIn->m_Comms, &ctx.m_hvToken);

	// verify payment confirmation signature
	GetPaymentConfirmationMsg(&ctx.m_hvMyID, &ctx.m_hvMyID, &ctx.m_hvToken, ctx.m_Txs.m_NetAmount, ctx.m_Txs.m_Aid);

	if (!Signature_IsValid_Ex(&pIn->m_PaymentProof, &ctx.m_hvMyID, &pIn->m_Mut.m_Peer))
		return MakeStatus(c_KeyKeeper_Status_Unspecified, 25);

	// 2nd user confirmation request. Now the kernel is complete, its ID is calculated
	ctx.m_Txs.m_Flags = c_KeyKeeper_ConfirmSpend_2ndPhase;
	ctx.m_Txs.m_pKrnID = &ctx.m_hvMyID;

	errCode = KeyKeeper_ConfirmSpend(p, &ctx.m_Txs);
	if (c_KeyKeeper_Status_Ok != errCode)
		return errCode;

	// Regenerate the slot (BEFORE signing), and sign
	KeyKeeper_RegenerateSlot(p, ctx.m_iSlot);

	Kernel_SignPartial(&pOut->m_TxSig.m_kSig, &pIn->m_Comms, &ctx.m_hvToken, &ctx.m_Keys); // safe to use pOut and pIn

	TxAggr_ToOffsetEx(p, &ctx.m_Keys.m_kKrn, &pOut->m_TxSig.m_kOffset);

	return c_KeyKeeper_Status_Ok;
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

#ifdef BeamCrypto_ScarceStack
#	define c_MultiMac_nBits_Offline 2
#else
#	define c_MultiMac_nBits_Offline 4
#endif // BeamCrypto_ScarceStack

#ifdef BeamCrypto_ExternalGej

typedef struct
{
	gej_t m_pPubGJG[3]; // gen.G, gen.J, ser.G

} ShieldedOfflineContext;

#else // BeamCrypto_ExternalGej

typedef struct
{
	secp256k1_ge_storage m_pPt[c_MultiMac_OddCount(c_MultiMac_nBits_Offline)]; // odd powers
} OfflineGenerator;

typedef struct
{
	OfflineGenerator m_pPubGJG[3]; // gen.G, gen.J, ser.G
	secp256k1_fe m_zDenom;

} ShieldedOfflineContext;

#endif // BeamCrypto_ExternalGej


typedef struct
{
	const UintBig* m_pGenSecret;
	const UintBig* m_pSerSecret;

	uint8_t m_IsOffline;

	union
	{
		ShieldedOfflineContext* m_pOffline;
		const ShieldedViewer* m_pViewer;
	} u;

} ShieldedVoucherContext;

void ShieldedVoucherContext_FromViewer(ShieldedVoucherContext* pCtx, const ShieldedViewer* pViewer)
{
	pCtx->m_IsOffline = 0;
	pCtx->u.m_pViewer = pViewer;
	pCtx->m_pGenSecret = &pViewer->m_Gen.m_Secret;
	pCtx->m_pSerSecret = &pViewer->m_Ser.m_Secret;
}

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
	wrap_scalar_mul(&pRes->m_Ser.m_kCoFactor, &pRes->m_Ser.m_kCoFactor, &sk);
}

static void MulGJ(gej_t* pGej, const secp256k1_scalar* pK)
{
#ifdef BeamCrypto_ExternalGej

	MulG(pGej, pK);

	gej_t gej;
	Gej_Init(&gej);
	MulJ(&gej, pK + 1);

	Gej_Add(pGej, pGej, &gej);
	Gej_Destroy(&gej);

#else // BeamCrypto_ExternalGej
	MultiMac_Context ctx;
	ctx.m_pRes = pGej;
	ctx.m_Fast.m_Count = 0;
	ctx.m_Secure.m_Count = 2;
	ctx.m_Secure.m_pGen = Context_get()->m_pGenGJ;
	ctx.m_Secure.m_pK = pK;

	MultiMac_Calculate(&ctx);
#endif // BeamCrypto_ExternalGej
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
	secp256k1_sha256_write_UintBig(&sha, &pVoucher->m_SharedSecret);
	secp256k1_sha256_finalize(&sha, pRes->m_pVal);
}

__stack_hungry__
static void ShieldedGetSpendKey(const ShieldedVoucherContext* pCtx, const secp256k1_scalar* pkG, uint8_t nIsGenByViewer, UintBig* pPreimage, secp256k1_scalar* pSk)
{
	secp256k1_sha256_t sha;
	ShieldedHashTxt(&sha);
	HASH_WRITE_STR(sha, "kG-k");
	secp256k1_scalar_get_b32(pPreimage->m_pVal, pkG);
	secp256k1_sha256_write_UintBig(&sha, pPreimage);
	secp256k1_sha256_finalize(&sha, pPreimage->m_pVal);

	if (nIsGenByViewer)
	{
		assert(!pCtx->m_IsOffline);
		Kdf_Derive_SKey(&pCtx->u.m_pViewer->m_Gen, pPreimage, pSk);
	}
	else
		KdfRaw_Derive_PKey(pCtx->m_pGenSecret, pPreimage, pSk);

	ShieldedHashTxt(&sha);
	HASH_WRITE_STR(sha, "k-pI");
	secp256k1_scalar_get_b32(pPreimage->m_pVal, pSk);
	secp256k1_sha256_write_UintBig(&sha, pPreimage);
	secp256k1_sha256_finalize(&sha, pPreimage->m_pVal); // SerialPreimage

	if (pCtx->m_IsOffline)
		KdfRaw_Derive_PKey(pCtx->m_pSerSecret, pPreimage, pSk); // should then be multiplied ser.PkG
	else
		Kdf_Derive_SKey(&pCtx->u.m_pViewer->m_Ser, pPreimage, pSk); // spend sk
}

__stack_hungry__
static void CreateVoucherInternal(const ShieldedVoucherContext* pCtx, ShieldedVoucher* pRes, const UintBig* pNonce)
{
	secp256k1_scalar pK[2], pN[2], sk;
	UintBig hv;
	Oracle oracle;

	// nonce -> kG
	ShieldedHashTxt(&oracle.m_sha);
	HASH_WRITE_STR(oracle.m_sha, "kG");
	secp256k1_sha256_write_UintBig(&oracle.m_sha, pNonce);
	secp256k1_sha256_finalize(&oracle.m_sha, hv.m_pVal);

	KdfRaw_Derive_PKey(pCtx->m_pGenSecret, &hv, pK);

	// kG -> serial preimage and spend sk
	ShieldedGetSpendKey(pCtx, pK, !pCtx->m_IsOffline, &hv, &sk);

	gej_t gej;
	Gej_Init(&gej);

#ifndef BeamCrypto_ExternalGej
	MultiMac_Context mmCtx;
#endif // BeamCrypto_ExternalGej

	if (pCtx->m_IsOffline)
	{
#ifdef BeamCrypto_ExternalGej

		Gej_MulFast(&gej, pCtx->u.m_pOffline->m_pPubGJG + 2, &sk); // ser.G
		Gej_Destroy(pCtx->u.m_pOffline->m_pPubGJG + 2);

#else // BeamCrypto_ExternalGej

		mmCtx.m_pRes = &gej;
		mmCtx.m_Secure.m_Count = 0;
		mmCtx.m_Fast.m_WndBits = c_MultiMac_nBits_Offline;
		mmCtx.m_Fast.m_pZDenom = &pCtx->u.m_pOffline->m_zDenom;

		MultiMac_WNaf wnaf;
		mmCtx.m_Fast.m_Count = 1;
		mmCtx.m_Fast.m_pK = &sk;
		mmCtx.m_Fast.m_pWnaf = &wnaf;
		mmCtx.m_Fast.m_pGen0 = pCtx->u.m_pOffline->m_pPubGJG[2].m_pPt; // ser.G

		MultiMac_Calculate(&mmCtx);

#endif // BeamCrypto_ExternalGej
	}
	else
		MulG(&gej, &sk); // spend pk

	Oracle_Init(&oracle);
	HASH_WRITE_STR(oracle.m_sha, "L.Spend");

	secp256k1_sha256_write_Gej(&oracle.m_sha, &gej);
	Oracle_NextScalar(&oracle, pK + 1); // serial

	MulGJ(&gej, pK); // kG*G + serial*J
	Point_Compact_from_Gej(&pRes->m_SerialPub, &gej);

	// DH
	ShieldedHashTxt(&oracle.m_sha);
	HASH_WRITE_STR(oracle.m_sha, "DH");
	secp256k1_sha256_write_CompactPoint(&oracle.m_sha, &pRes->m_SerialPub);
	secp256k1_sha256_finalize(&oracle.m_sha, hv.m_pVal);

	if (pCtx->m_IsOffline)
	{
		KdfRaw_Derive_PKey(pCtx->m_pGenSecret, &hv, &sk);
		wrap_scalar_mul(pN, pK, &sk);
		wrap_scalar_mul(pN + 1, pK + 1, &sk);

#ifdef BeamCrypto_ExternalGej

		Gej_MulFast(&gej, pCtx->u.m_pOffline->m_pPubGJG, pN);
		Gej_Destroy(pCtx->u.m_pOffline->m_pPubGJG);

		gej_t gej2;
		Gej_Init(&gej2);

		Gej_MulFast(&gej2, pCtx->u.m_pOffline->m_pPubGJG + 1, pN + 1);
		Gej_Destroy(pCtx->u.m_pOffline->m_pPubGJG + 1);

		Gej_Add(&gej, &gej, &gej2);

		Gej_Destroy(&gej2);

#else // BeamCrypto_ExternalGej
		MultiMac_WNaf pWnaf[2];

		mmCtx.m_Fast.m_Count = 2;
		mmCtx.m_Fast.m_pK = pN;
		mmCtx.m_Fast.m_pWnaf = pWnaf;
		mmCtx.m_Fast.m_pGen0 = pCtx->u.m_pOffline->m_pPubGJG[0].m_pPt; // gen.G, gen.J

		MultiMac_Calculate(&mmCtx);
#endif // BeamCrypto_ExternalGej

	}
	else
	{
		Kdf_Derive_SKey(&pCtx->u.m_pViewer->m_Gen, &hv, &sk); // DH multiplier
		wrap_scalar_mul(pN, pK, &sk);
		wrap_scalar_mul(pN + 1, pK + 1, &sk);
		MulGJ(&gej, pN); // shared point
	}

	ShieldedHashTxt(&oracle.m_sha);
	HASH_WRITE_STR(oracle.m_sha, "sp-sec");
	secp256k1_sha256_write_Gej(&oracle.m_sha, &gej);
	secp256k1_sha256_finalize(&oracle.m_sha, pRes->m_SharedSecret.m_pVal); // Shared secret

	// nonces
	ShieldedHashTxt(&oracle.m_sha);
	HASH_WRITE_STR(oracle.m_sha, "nG");
	secp256k1_sha256_write_UintBig(&oracle.m_sha, &pRes->m_SharedSecret);
	secp256k1_sha256_finalize(&oracle.m_sha, hv.m_pVal);

	KdfRaw_Derive_PKey(pCtx->m_pGenSecret, &hv, pN);

	ShieldedHashTxt(&oracle.m_sha);
	HASH_WRITE_STR(oracle.m_sha, "nJ");
	secp256k1_sha256_write_UintBig(&oracle.m_sha, &pRes->m_SharedSecret);
	secp256k1_sha256_finalize(&oracle.m_sha, hv.m_pVal);

	KdfRaw_Derive_PKey(pCtx->m_pGenSecret, &hv, pN + 1);

	MulGJ(&gej, pN); // nG*G + nJ*J
	Point_Compact_from_Gej(&pRes->m_NoncePub, &gej);

	// sign it
	Ticket_Hash(&hv, pRes);
	Signature_GetChallengeEx(&pRes->m_NoncePub, &hv, &sk);
	Signature_SignPartialEx(pRes->m_pK, &sk, pK, pN);
	Signature_SignPartialEx(pRes->m_pK + 1, &sk, pK + 1, pN + 1);

	Gej_Destroy(&gej);
}

PROTO_METHOD(CreateShieldedVouchers)
{
	PROTO_UNUSED_ARGS;

	if (nIn)
		return c_KeyKeeper_Status_ProtoError;

	AddrID addrID;
	N2H_uint(addrID, pIn->m_AddrID, 64);

	uint8_t nCount = pIn->m_Count;
	if (!nCount)
		return c_KeyKeeper_Status_Ok;

	if (nOut < sizeof(ShieldedVoucher))
		return MakeStatus(c_KeyKeeper_Status_ProtoError, 1);
	*pOutSize += sizeof(ShieldedVoucher);

	const uint8_t nMaxCountAux = sizeof(KeyKeeper_AuxBuf) / sizeof(ShieldedVoucher);
	if (nCount - 1 > nMaxCountAux)
		return MakeStatus(c_KeyKeeper_Status_ProtoError, 2);

	ShieldedViewer viewer;
	ShieldedViewerInit(&viewer, 0, p);

	ShieldedVoucherContext vCtx;
	ShieldedVoucherContext_FromViewer(&vCtx, &viewer);

	// key to sign the voucher(s)
	UintBig hv, hvNonce;
	secp256k1_scalar skSign;
	DeriveAddress(p, addrID, &skSign, &hv);

	ShieldedVoucher* pRes = (ShieldedVoucher*)(pOut + 1); // no fields require alignment
	memcpy(&hvNonce, &pIn->m_Nonce0, sizeof(hvNonce));

	for (uint32_t i = 0; ; )
	{
		CreateVoucherInternal(&vCtx, pRes, &hvNonce);

		Voucher_Hash(&hv, pRes);
		Signature_Sign(&pRes->m_Signature, &hv, &skSign);

		if (++i == nCount)
			break;

		// append the result to aux buf
		KeyKeeper_WriteAuxBuf(p, pRes, sizeof(*pRes) * (i - 1), sizeof(*pRes));

		// regenerate nonce
		Oracle oracle;
		secp256k1_sha256_initialize(&oracle.m_sha);
		HASH_WRITE_STR(oracle.m_sha, "sh.v.n");
		secp256k1_sha256_write_UintBig(&oracle.m_sha, &hvNonce);
		secp256k1_sha256_finalize(&oracle.m_sha, hvNonce.m_pVal);
	}

	return c_KeyKeeper_Status_Ok;
}

//////////////////////////////
// KeyKeeper - SignOfflineAddr
void OfflineAddr_Init(OfflineAddr* pRes, const KeyKeeper* p, uint32_t iAddr)
{
	ShieldedViewer viewer;
	ShieldedViewerInit(&viewer, iAddr, p);

	pRes->m_Gen_Secret = viewer.m_Gen.m_Secret;
	pRes->m_Ser_Secret = viewer.m_Ser.m_Secret;

	gej_t pGej[3];
	Gej_Init(pGej);
	Gej_Init(pGej + 1);
	Gej_Init(pGej + 2);

	MulG(pGej, &viewer.m_Gen.m_kCoFactor);
	MulJ(pGej + 1, &viewer.m_Gen.m_kCoFactor);
	MulG(pGej + 2, &viewer.m_Ser.m_kCoFactor);

#ifdef BeamCrypto_ExternalGej
	Point_Compact_from_Gej(&pRes->m_Gen_PkG, pGej);
	Point_Compact_from_Gej(&pRes->m_Gen_PkJ, pGej + 1);
	Point_Compact_from_Gej(&pRes->m_Ser_PkG, pGej + 2);
#else // BeamCrypto_ExternalGej
	secp256k1_fe pBuf[_countof(pGej)];
	secp256k1_fe zDenom;
	Point_Gej_BatchRescale(pGej, _countof(pGej), pBuf, &zDenom, 1);

	Point_Compact_from_Ge(&pRes->m_Gen_PkG, (secp256k1_ge*) pGej);
	Point_Compact_from_Ge(&pRes->m_Gen_PkJ, (secp256k1_ge*) (pGej + 1));
	Point_Compact_from_Ge(&pRes->m_Ser_PkG, (secp256k1_ge*) (pGej + 2));
#endif // BeamCrypto_ExternalGej

	Gej_Destroy(pGej + 2);
	Gej_Destroy(pGej + 1);
	Gej_Destroy(pGej);
}

__stack_hungry__
void OfflineAddr_getHash(UintBig* pRes, const OfflineAddr* pAddr)
{
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);
	HASH_WRITE_STR(sha, "sh.pub.sig");
	secp256k1_sha256_write_UintBig(&sha, &pAddr->m_Gen_Secret);
	secp256k1_sha256_write_CompactPoint(&sha, &pAddr->m_Gen_PkG);
	secp256k1_sha256_write_CompactPoint(&sha, &pAddr->m_Gen_PkJ);
	secp256k1_sha256_write_UintBig(&sha, &pAddr->m_Ser_Secret);
	secp256k1_sha256_write_CompactPoint(&sha, &pAddr->m_Ser_PkG);

	secp256k1_sha256_finalize(&sha, pRes->m_pVal);
}

PROTO_METHOD(SignOfflineAddr)
{
	PROTO_UNUSED_ARGS;

	if (nIn)
		return c_KeyKeeper_Status_ProtoError;

	AddrID addrID;
	N2H_uint(addrID, pIn->m_AddrID, 64);

	UintBig hv;
	secp256k1_scalar sk;
	DeriveAddress(p, addrID, &sk, &hv);

	ShieldedViewer viewer;
	ShieldedViewerInit(&viewer, 0, p);

	OfflineAddr addr;
	OfflineAddr_Init(&addr, p, 0);

	OfflineAddr_getHash(&hv, &addr);

	Signature_Sign(&pOut->m_Signature, &hv, &sk);

	SECURE_ERASE_OBJ(sk);

	return c_KeyKeeper_Status_Ok;
}

//////////////////////////////
// KeyKeeper - CreateShieldedInput
PROTO_METHOD(CreateShieldedInput_1)
{
	PROTO_UNUSED_ARGS;

	ShieldedInput_Fmt fmt;
	N2H_ShieldedInput_Fmt(&fmt, &pIn->m_InpFmt);

	ShieldedInput_SpendParams sip;
	memcpy_unaligned(&sip, &pIn->m_SpendParams, sizeof(sip));
	N2H_uint_inplace(sip.m_hMin, 64);
	N2H_uint_inplace(sip.m_hMax, 64);
	N2H_uint_inplace(sip.m_WindowEnd, 64);
	N2H_uint_inplace(sip.m_Sigma_M, 32);
	N2H_uint_inplace(sip.m_Sigma_n, 32);

	if (!sip.m_Sigma_M)
		return MakeStatus(c_KeyKeeper_Status_Unspecified, 22); // Attacker may try to read uninitialized mem

	// calculate kernel Msg

	Oracle* const pOracle = &p->u.m_Ins.m_Oracle;
	TxKernel_SpecialMsg(&pOracle->m_sha, fmt.m_Fee, sip.m_hMin, sip.m_hMax, 4);
	secp256k1_sha256_write_Num(&pOracle->m_sha, sip.m_WindowEnd);

	UintBig hv;
	secp256k1_sha256_finalize(&pOracle->m_sha, hv.m_pVal);

	// init oracle
	secp256k1_sha256_initialize(&pOracle->m_sha);
	secp256k1_sha256_write_UintBig(&pOracle->m_sha, &hv);

	// starting from HF3 commitmens to shielded state and asset are mandatory
	secp256k1_sha256_write_UintBig(&pOracle->m_sha, &pIn->m_ShieldedState);
	secp256k1_sha256_write_CompactPointOptional2(&pOracle->m_sha, &pIn->m_ptAssetGen, !IsUintBigZero(&pIn->m_ptAssetGen.m_X));

	secp256k1_sha256_write_Num(&pOracle->m_sha, sip.m_Sigma_n);
	secp256k1_sha256_write_Num(&pOracle->m_sha, sip.m_Sigma_M);

	// Derive keys

	// Output blinding factor
	ShieldedInput_getSk(p, &pIn->m_InpBlob, &fmt, &p->u.m_Ins.m_skOutp);

	// spend sk/pk
	int overflow;
	secp256k1_scalar_set_b32(&p->u.m_Ins.m_skSpend, pIn->m_InpBlob.m_kSerG.m_pVal, &overflow);
	if (overflow)
		return MakeStatus(c_KeyKeeper_Status_Unspecified, 21);

	ShieldedViewer viewer;
	ShieldedViewerInit(&viewer, fmt.m_nViewerIdx, p);

	ShieldedVoucherContext vCtx;
	ShieldedVoucherContext_FromViewer(&vCtx, &viewer);

	ShieldedGetSpendKey(&vCtx, &p->u.m_Ins.m_skSpend, pIn->m_InpBlob.m_IsCreatedByViewer, &hv, &p->u.m_Ins.m_skSpend);

	// output commitment
	CustomGenerator aGen;
#ifdef BeamCrypto_ExternalGej
	Gej_Init(&aGen);
#endif // BeamCrypto_ExternalGej

	if (fmt.m_AssetID)
		CoinID_GenerateAGen(fmt.m_AssetID, &aGen);

	gej_t gej;
	Gej_Init(&gej);

	CoinID_getCommRaw(&p->u.m_Ins.m_skOutp, fmt.m_Amount, fmt.m_AssetID ? &aGen : 0, &gej);
	secp256k1_sha256_write_Gej(&pOracle->m_sha, &gej);

#ifdef BeamCrypto_ExternalGej
	Gej_Destroy(&aGen);
#endif // BeamCrypto_ExternalGej

	// Spend pk
	MulG(&gej, &p->u.m_Ins.m_skSpend);
	secp256k1_sha256_write_Gej(&pOracle->m_sha, &gej);
	Gej_Destroy(&gej);

	// finalyze
	p->u.m_Ins.m_Sigma_M = sip.m_Sigma_M;
	p->u.m_Ins.m_Remaining = sip.m_Sigma_M;

	p->m_State = c_KeyKeeper_State_CreateShielded_1;
	return c_KeyKeeper_Status_Ok;
}

PROTO_METHOD(CreateShieldedInput_2)
{
	PROTO_UNUSED_ARGS;

	if (c_KeyKeeper_State_CreateShielded_1 != p->m_State)
		return MakeStatus(c_KeyKeeper_Status_Unspecified, 20);

	// Generate sigGen
	Oracle* const pOracle = &p->u.m_Ins.m_Oracle;

	UintBig hvSigGen;
	Oracle_NextHash(pOracle, &hvSigGen);

	// update oracle (consume input before generating output)
	for (uint32_t i = 0; i < _countof(pIn->m_pABCD); i++)
		secp256k1_sha256_write_CompactPoint(&pOracle->m_sha, pIn->m_pABCD + i);

	secp256k1_scalar k;

	{
		// derive nonce. Sensitive commitments (corresponding to secret keys) were already exposed
		NonceGenerator ng;
		static const char szSalt[] = "beam.lelantus.1";

		secp256k1_hmac_sha256_t hmac;
		NonceGenerator_InitBegin(&ng, &hmac, szSalt, sizeof(szSalt));

		secp256k1_hmac_sha256_write_UintBig(&hmac, &hvSigGen);
		secp256k1_hmac_sha256_write_CompactPoint(&hmac, &pIn->m_NoncePub);

		NonceGenerator_InitEnd(&ng, &hmac);

		NonceGenerator_NextScalar(&ng, &k);
		SECURE_ERASE_OBJ(ng);
	}


	{
		gej_t gej;
		Gej_Init(&gej);
		secp256k1_ge ge;

		secp256k1_scalar e;

		MulG(&gej, &k);

		if (!Point_Ge_from_Compact(&ge, &pIn->m_NoncePub))
			return MakeStatus(c_KeyKeeper_Status_Unspecified, 22); // import failed

		wrap_gej_add_ge_var(&gej, &gej, &ge);

		Point_Ge_from_Gej(&ge, &gej);
		Point_Compact_from_Ge(&pOut->m_NoncePub, &ge);
		Gej_Destroy(&gej);

		Oracle o2;
		Oracle_Init(&o2);
		secp256k1_sha256_write_CompactPoint(&o2.m_sha, &pOut->m_NoncePub);
		secp256k1_sha256_write_UintBig(&o2.m_sha, &hvSigGen);

		// 1st challenge
		Oracle_NextScalar(&o2, &e);

		wrap_scalar_mul(&e, &p->u.m_Ins.m_skOutp, &e);
		secp256k1_scalar_add(&k, &k, &e); // nG += skOutp * e

		// 2nd challenge
		Oracle_NextScalar(&o2, &e);
		wrap_scalar_mul(&e, &p->u.m_Ins.m_skSpend, &e);
		secp256k1_scalar_add(&k, &k, &e); // nG += skSpend * e

		// to sig
		secp256k1_scalar_negate(&k, &k);
		secp256k1_scalar_get_b32(pOut->m_SigG.m_pVal, &k);
	}

	p->m_State = c_KeyKeeper_State_CreateShielded_2;
	return c_KeyKeeper_Status_Ok;
}

PROTO_METHOD(CreateShieldedInput_3)
{
	PROTO_UNUSED_ARGS;

	if (c_KeyKeeper_State_CreateShielded_2 != p->m_State)
		return MakeStatus(c_KeyKeeper_Status_Unspecified, 20);

	if (pIn->m_NumPoints >= p->u.m_Ins.m_Remaining)
		return MakeStatus(c_KeyKeeper_Status_ProtoError, 21);

	CompactPoint* pG = (CompactPoint*)(pIn + 1);
	if (nIn != sizeof(*pG) * pIn->m_NumPoints)
		return c_KeyKeeper_Status_ProtoError;

	Oracle* const pOracle = &p->u.m_Ins.m_Oracle;

	for (uint32_t i = 0; i < pIn->m_NumPoints; i++)
		secp256k1_sha256_write_CompactPoint(&pOracle->m_sha, pG + i);

	p->u.m_Ins.m_Remaining -= pIn->m_NumPoints;
	return c_KeyKeeper_Status_Ok;
}

PROTO_METHOD(CreateShieldedInput_4)
{
	PROTO_UNUSED_ARGS;

	if (c_KeyKeeper_State_CreateShielded_2 != p->m_State)
		return MakeStatus(c_KeyKeeper_Status_Unspecified, 20);

	uint32_t nRemaining = p->u.m_Ins.m_Remaining;
	assert(nRemaining);

	CompactPoint* pG = (CompactPoint*)(pIn + 1);
	if (nIn != sizeof(*pG) * nRemaining)
		return c_KeyKeeper_Status_ProtoError;


	Oracle* const pOracle = &p->u.m_Ins.m_Oracle;

	for (uint32_t i = 0; i < nRemaining - 1; i++)
		secp256k1_sha256_write_CompactPoint(&pOracle->m_sha, pG + i);

	const CompactPoint* pG_Last = pG + nRemaining - 1;

	// derive nonce
	secp256k1_scalar k;

	{
		// hash all the visible to-date params
		union {
			secp256k1_sha256_t sha;
			NonceGenerator ng;
		} u;

		u.sha = pOracle->m_sha; // copy
		secp256k1_sha256_write_CompactPoint(&u.sha, pG_Last);

		UintBig hv;
		secp256k1_sha256_finalize(&u.sha, hv.m_pVal);

		// derive nonce. Sensitive commitments (corresponding to secret keys) were already exposed
		static const char szSalt[] = "beam.lelantus.2";
		NonceGenerator_Init(&u.ng, szSalt, sizeof(szSalt), &hv);

		NonceGenerator_NextScalar(&u.ng, &k);

		SECURE_ERASE_OBJ(u);
	}

	gej_t gej;
	secp256k1_ge ge;

	if (!Point_Ge_from_Compact(&ge, pG_Last))
		return MakeStatus(c_KeyKeeper_Status_Unspecified, 22); // import failed

	Gej_Init(&gej);
	MulG(&gej, &k);
	wrap_gej_add_ge_var(&gej, &gej, &ge);

	Point_Ge_from_Gej(&ge, &gej);
	Point_Compact_from_Ge(&pOut->m_G_Last, &ge);
	secp256k1_sha256_write_CompactPoint(&pOracle->m_sha, &pOut->m_G_Last);

	Gej_Destroy(&gej);

	secp256k1_scalar e, xPwr;
	Oracle_NextScalar(pOracle, &e);

	// calculate zR
	nRemaining = p->u.m_Ins.m_Sigma_M;
	xPwr = e;
	for (uint32_t i = 1; i < nRemaining; i++)
	{
		if (i == nRemaining - 1)
			wrap_scalar_mul(&k, &k, &xPwr); // tau * e^(N-1)

		wrap_scalar_mul(&xPwr, &xPwr, &e);
	}

	wrap_scalar_mul(&xPwr, &p->u.m_Ins.m_skOutp, &xPwr); // sk * e^N

	secp256k1_scalar_add(&k, &k, &xPwr); // sk * e^N + tau * e^(N-1)
	secp256k1_scalar_negate(&k, &k);

	secp256k1_scalar_get_b32(pOut->m_zR.m_pVal, &k);

	SECURE_ERASE_OBJ(p->u.m_Ins);
	p->m_State = 0;

	return c_KeyKeeper_Status_Ok;
}

//////////////////////////////
// KeyKeeper - Aux I/O
PROTO_METHOD(AuxWrite)
{
	PROTO_UNUSED_ARGS;

	uint16_t nOffset, nSize;

	N2H_uint(nOffset, pIn->m_Offset, 16);
	N2H_uint(nSize, pIn->m_Size, 16);

	if (nSize != nIn)
		return c_KeyKeeper_Status_ProtoError;

	uint16_t nEnd = nOffset + nSize;
	if (nEnd < nSize)
		return MakeStatus(c_KeyKeeper_Status_Unspecified, 11); // overflow

	if (nEnd > sizeof(KeyKeeper_AuxBuf))
		return MakeStatus(c_KeyKeeper_Status_Unspecified, 12);

	KeyKeeper_WriteAuxBuf(p, pIn + 1, nOffset, nSize);

	return c_KeyKeeper_Status_Ok;
}

PROTO_METHOD(AuxRead)
{
	PROTO_UNUSED_ARGS;

	if (nIn)
		return c_KeyKeeper_Status_ProtoError;

	uint16_t nOffset, nSize;

	N2H_uint(nOffset, pIn->m_Offset, 16);
	N2H_uint(nSize, pIn->m_Size, 16);

	if (nOut < nSize)
		return c_KeyKeeper_Status_ProtoError;

	uint16_t nEnd = nOffset + nSize;
	if (nEnd < nSize)
		return MakeStatus(c_KeyKeeper_Status_Unspecified, 11); // overflow

	if (nEnd > sizeof(KeyKeeper_AuxBuf))
		return MakeStatus(c_KeyKeeper_Status_Unspecified, 12);

	const uint8_t* pSrc = (const uint8_t*) KeyKeeper_GetAuxBuf(p);

	memcpy(pOut + 1, pSrc + nOffset, nSize);
	*pOutSize += nSize;

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


typedef struct
{
	KeyKeeper* m_p;
	const OpIn_TxSendShielded* m_pIn;
	OpOut_TxSendShielded* m_pOut;
	const ShieldedOutParams* m_pSh;

	TxSummary m_Txs;

	secp256k1_scalar m_skKrn;
	UintBig m_hvKrn;

} TxSendShieldedContext;

int TxSendShielded_VoucherCheck(TxSendShieldedContext* pCtx)
{
	// check the voucher
	Voucher_Hash(&pCtx->m_hvKrn, &pCtx->m_pSh->u.m_Voucher);
	return Signature_IsValid_Ex(&pCtx->m_pSh->u.m_Voucher.m_Signature, &pCtx->m_hvKrn, &pCtx->m_pIn->m_Mut.m_Peer);
}

int TxSendShielded_OfflineAddrCheck(TxSendShieldedContext* pCtx)
{
	OfflineAddr_getHash(&pCtx->m_hvKrn, &pCtx->m_pSh->u.m_Offline.m_Addr);
	return Signature_IsValid_Ex(&pCtx->m_pSh->u.m_Offline.m_Sig, &pCtx->m_hvKrn, &pCtx->m_pIn->m_Mut.m_Peer);
}

#ifdef BeamCrypto_ExternalGej

int Point_Gej_from_CompactNnz(gej_t* pGej, const CompactPoint* pPt)
{
	secp256k1_ge ge;
	if (!Point_Ge_from_CompactNnz(&ge, pPt))
		return 0;

	Point_Gej_from_Ge(pGej, &ge);
	return 1;
}

int TxSendShielded_ImportGens(ShieldedOfflineContext* pOff, TxSendShieldedContext* pCtx)
{
	return
		Point_Gej_from_CompactNnz(pOff->m_pPubGJG, &pCtx->m_pSh->u.m_Offline.m_Addr.m_Gen_PkG) &&
		Point_Gej_from_CompactNnz(pOff->m_pPubGJG + 1, &pCtx->m_pSh->u.m_Offline.m_Addr.m_Gen_PkJ) &&
		Point_Gej_from_CompactNnz(pOff->m_pPubGJG + 2, &pCtx->m_pSh->u.m_Offline.m_Addr.m_Ser_PkG);
}

#else // BeamCrypto_ExternalGej

__stack_hungry__
int TxSendShielded_ImportGen(gej_t* pOdds, const CompactPoint* pPt)
{
	secp256k1_ge ge;
	if (!Point_Ge_from_CompactNnz(&ge, pPt))
		return 0;

	Point_CalculateOdds(pOdds, c_MultiMac_OddCount(c_MultiMac_nBits_Offline), &ge);
	return 1;
}

__stack_hungry__
int TxSendShielded_ImportGens(ShieldedOfflineContext* pOff, TxSendShieldedContext* pCtx)
{
	gej_t pOdds[_countof(pOff->m_pPubGJG->m_pPt) * 3];

	if (!TxSendShielded_ImportGen(pOdds, &pCtx->m_pSh->u.m_Offline.m_Addr.m_Gen_PkG) ||
		!TxSendShielded_ImportGen(pOdds + _countof(pOff->m_pPubGJG->m_pPt), &pCtx->m_pSh->u.m_Offline.m_Addr.m_Gen_PkJ) ||
		!TxSendShielded_ImportGen(pOdds + _countof(pOff->m_pPubGJG->m_pPt) * 2, &pCtx->m_pSh->u.m_Offline.m_Addr.m_Ser_PkG))
		return 0;

	Point_Gej_ToCommonDenominator(pOdds, _countof(pOdds), pOff->m_pPubGJG->m_pPt, &pOff->m_zDenom);
	return 1;
}

#endif // BeamCrypto_ExternalGej

__stack_hungry__
void TxSendShielded_OfflineMakeTicket2(TxSendShieldedContext* pCtx, ShieldedOfflineContext* pOff)
{
	ShieldedVoucherContext vCtx;
	vCtx.m_IsOffline = 1;
	vCtx.u.m_pOffline = pOff;
	vCtx.m_pGenSecret = &pCtx->m_pSh->u.m_Offline.m_Addr.m_Gen_Secret;
	vCtx.m_pSerSecret = &pCtx->m_pSh->u.m_Offline.m_Addr.m_Ser_Secret;

	ShieldedVoucher voucher;
	CreateVoucherInternal(&vCtx, &voucher, &pCtx->m_pSh->u.m_Offline.m_Nonce);

	KeyKeeper_WriteAuxBuf(pCtx->m_p, &voucher, offsetof(ShieldedOutParams, u.m_Voucher), sizeof(voucher));
}

__stack_hungry__
int TxSendShielded_OfflineMakeTicket(TxSendShieldedContext* pCtx)
{
	ShieldedOfflineContext wrk;

#ifdef BeamCrypto_ExternalGej
	for (uint32_t i = 0; i < _countof(wrk.m_pPubGJG); i++)
		Gej_Init(wrk.m_pPubGJG + i);
#endif // BeamCrypto_ExternalGej

	int res = TxSendShielded_ImportGens(&wrk, pCtx);
	if (res)
		TxSendShielded_OfflineMakeTicket2(pCtx, &wrk);

#ifdef BeamCrypto_ExternalGej
	for (uint32_t i = _countof(wrk.m_pPubGJG); i--; )
		Gej_Destroy(wrk.m_pPubGJG + i);
#endif // BeamCrypto_ExternalGej

	return res;
}

typedef struct
{
	union
	{
		RangeProof_Recovery_Context m_RCtx;
		CustomGenerator m_AGen;
	} u;
	uint8_t m_FlagsPacked;

} TxSendShieldedRecoveryParams;

__stack_hungry__
void TxSendShielded_PrepareRangeProofRepack(TxSendShieldedContext* pCtx, TxSendShieldedRecoveryParams* pRp, Oracle* pOracle)
{
	RangeProof_Repacked rep;
	RangeProof_Recover_Init(&pCtx->m_pSh->m_RangeProof, pOracle, &pRp->u.m_RCtx, &rep);

	// Before we repack (i.e. overwrite the original RangeProof) - finalyze the krnID
	secp256k1_sha256_initialize(&pOracle->m_sha);
	secp256k1_sha256_write_UintBig(&pOracle->m_sha, &pCtx->m_hvKrn);
	secp256k1_sha256_write(&pOracle->m_sha, (uint8_t*) &pCtx->m_pSh->m_RangeProof, sizeof(pCtx->m_pSh->m_RangeProof));
	secp256k1_sha256_finalize(&pOracle->m_sha, pCtx->m_hvKrn.m_pVal);

	// Overwrite it with repacked version
	const KeyKeeper_AuxBuf* pAuxBuf = KeyKeeper_GetAuxBuf(pCtx->m_p);

	static_assert(sizeof(RangeProof_Packed) >= sizeof(RangeProof_Repacked) + 7, "");
	uint32_t nOffs = (uint32_t) (((uint8_t*) &pAuxBuf->m_Sh.m_RangeProof) - ((uint8_t*) pAuxBuf));
	nOffs = (nOffs + 7) & ~7;

	// overwrite our Rangeproof with its repacked version
	KeyKeeper_WriteAuxBuf(pCtx->m_p, &rep, nOffs, sizeof(rep));

	pRp->u.m_RCtx.m_pRepack = (RangeProof_Repacked*)(((uint8_t*)pAuxBuf) + nOffs);

}

__stack_hungry__
void TxSendShielded_PrepareRangeProofRecover(TxSendShieldedContext* pCtx, TxSendShieldedRecoveryParams* pRp)
{
	union {
		NonceGenerator ng; // no really secret
		Oracle o2;
		gej_t gej;
		secp256k1_scalar skExtra;
	} u;

	{
		static const char szSalt[] = "kG-O";
		NonceGenerator_Init(&u.ng, szSalt, sizeof(szSalt), &pCtx->m_pSh->u.m_Voucher.m_SharedSecret);
		NonceGenerator_NextScalar(&u.ng, &pCtx->m_skKrn);
	}

	pRp->m_FlagsPacked = Msg2Scalar(&u.skExtra, &pCtx->m_pIn->m_User.m_Sender);
	secp256k1_scalar_add(&pCtx->m_skKrn, &pCtx->m_skKrn, &u.skExtra); // output blinding factor

	CustomGenerator* pAGen = pCtx->m_Txs.m_Aid ? &pRp->u.m_AGen : 0;
	if (pAGen)
	{
#ifdef BeamCrypto_ExternalGej
		Gej_Init(pAGen);
#endif // BeamCrypto_ExternalGej
		CoinID_GenerateAGen(pCtx->m_Txs.m_Aid, pAGen); // assume that's not the peak stack consumer
	}

	Gej_Init(&u.gej);
	CoinID_getCommRaw(&pCtx->m_skKrn, pCtx->m_Txs.m_NetAmount, pAGen, &u.gej); // output commitment

#ifdef BeamCrypto_ExternalGej
	if (pAGen)
		Gej_Destroy(pAGen);
#endif // BeamCrypto_ExternalGej

	// We have the commitment, and params that are supposed to be packed in the rangeproof.
	// Recover the parameters, make sure they match

	Oracle oracle;
	TxKernel_SpecialMsg(&oracle.m_sha, 0, 0, -1, 3);
	secp256k1_sha256_finalize(&oracle.m_sha, pCtx->m_hvKrn.m_pVal); // krn.Msg

	secp256k1_sha256_initialize(&oracle.m_sha);
	secp256k1_sha256_write_UintBig(&oracle.m_sha, &pCtx->m_hvKrn); // oracle << krn.Msg
	secp256k1_sha256_write_CompactPoint(&oracle.m_sha, &pCtx->m_pSh->u.m_Voucher.m_SerialPub);
	secp256k1_sha256_write_CompactPoint(&oracle.m_sha, &pCtx->m_pSh->u.m_Voucher.m_NoncePub);
	secp256k1_sha256_write_Gej(&oracle.m_sha, &u.gej);
	secp256k1_sha256_write_CompactPointOptional2(&oracle.m_sha, &pCtx->m_pIn->m_ptAssetGen, !IsUintBigZero(&pCtx->m_pIn->m_ptAssetGen.m_X)); // starting from HF3 it's mandatory

	Gej_Destroy(&u.gej);

	{
		u.o2 = oracle;
		HASH_WRITE_STR(u.o2.m_sha, "bp-s");
		secp256k1_sha256_write_UintBig(&u.o2.m_sha, &pCtx->m_pSh->u.m_Voucher.m_SharedSecret);
		secp256k1_sha256_finalize(&u.o2.m_sha, pRp->u.m_RCtx.m_Seed.m_pVal); // rangeproof seed. For both gen and blinding factor
	}

	TxSendShielded_PrepareRangeProofRepack(pCtx, pRp, &oracle);
}

__stack_hungry__
uint16_t TxSendShielded_VerifyParams2(TxSendShieldedContext* pCtx, TxSendShieldedRecoveryParams* pRp)
{

#pragma pack (push, 1)
	typedef struct
	{
		AssetID m_AssetID;
		uint8_t m_Flags;
	} ShieldedTxo_RangeProof_Packed;
#pragma pack (pop)

	ShieldedTxo_RangeProof_Packed packed;

	pRp->u.m_RCtx.m_pUser = &packed;
	pRp->u.m_RCtx.m_nUser = sizeof(packed);

	if (!RangeProof_Recover1(&pRp->u.m_RCtx))
		return MakeStatus(c_KeyKeeper_Status_Unspecified, 26);

	if ((pRp->u.m_RCtx.m_Amount != pCtx->m_Txs.m_NetAmount) || (bswap32_be(packed.m_AssetID) != pCtx->m_Txs.m_Aid))
		return MakeStatus(c_KeyKeeper_Status_Unspecified, 27);

	pRp->m_FlagsPacked ^= packed.m_Flags;

	return c_KeyKeeper_Status_Ok;
}

__stack_hungry__
uint16_t TxSendShielded_VerifyParams3(TxSendShieldedContext* pCtx, TxSendShieldedRecoveryParams* pRp)
{
	{
		secp256k1_scalar pExtra[2];
		pRp->m_FlagsPacked ^= (Msg2Scalar(pExtra, &pCtx->m_pIn->m_User.m_pMessage[0]) << 1);
		pRp->m_FlagsPacked ^= (Msg2Scalar(pExtra + 1, &pCtx->m_pIn->m_User.m_pMessage[1]) << 2);

		if (memcmp(pRp->u.m_RCtx.m_pExtra, pExtra, sizeof(pExtra)) || pRp->m_FlagsPacked)
			return MakeStatus(c_KeyKeeper_Status_Unspecified, 28);

	}

	if (pCtx->m_Txs.m_Aid || pCtx->m_pIn->m_HideAssetAlways)
	{
		static const char szSalt[] = "skG-O";
		NonceGenerator ng; // not really secret
		NonceGenerator_Init(&ng, szSalt, sizeof(szSalt), &pCtx->m_pSh->u.m_Voucher.m_SharedSecret);
		NonceGenerator_NextScalar(&ng, pRp->u.m_RCtx.m_pExtra);

		secp256k1_scalar_set_u64(pRp->u.m_RCtx.m_pExtra + 1, pCtx->m_Txs.m_NetAmount);
		wrap_scalar_mul(pRp->u.m_RCtx.m_pExtra, pRp->u.m_RCtx.m_pExtra, pRp->u.m_RCtx.m_pExtra + 1);
		secp256k1_scalar_add(&pRp->u.m_RCtx.m_Sk, &pRp->u.m_RCtx.m_Sk, pRp->u.m_RCtx.m_pExtra);
	}

	if (memcmp(&pCtx->m_skKrn, &pRp->u.m_RCtx.m_Sk, sizeof(pRp->u.m_RCtx.m_Sk)))
		return MakeStatus(c_KeyKeeper_Status_Unspecified, 28);

	// all match! Calculate the resulting kernelID
	return c_KeyKeeper_Status_Ok;
}

__stack_hungry__
uint16_t TxSendShielded_VerifyParams(TxSendShieldedContext* pCtx)
{
	// The host expects a TxKernelStd, which contains TxKernelShieldedOutput as a nested kernel, which contains the ShieldedTxo
	// This ShieldedTxo must use the ticket from the voucher, have appropriate commitment + rangeproof, which MUST be generated w.r.t. SharedSecret from the voucher.
	// The receiver *MUST* be able to decode all the parameters w.r.t. the protocol.
	//
	// So, here we ensure that the protocol is obeyed, i.e. the receiver will be able to decode all the parameters from the rangeproof
	//
	// Important: the TxKernelShieldedOutput kernel ID has the whole rangeproof serialized, means it can't be tampered with after we sign the outer kernel

	TxSendShieldedRecoveryParams rp;
	TxSendShielded_PrepareRangeProofRecover(pCtx, &rp);

	uint16_t errCode = TxSendShielded_VerifyParams2(pCtx, &rp);
	if (errCode)
		return errCode;

	RangeProof_Recover2(&rp.u.m_RCtx);
	RangeProof_Recover3(&rp.u.m_RCtx);

	return TxSendShielded_VerifyParams3(pCtx, &rp);
}

__stack_hungry__
uint16_t TxSendShielded_FinalyzeTx(TxSendShieldedContext* pCtx, int bSplit)
{
	// select blinding factor for the outer kernel.
	UintBig hvOuter, hvPeer;
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);
	secp256k1_sha256_write_UintBig(&sha, &pCtx->m_hvKrn);
	secp256k1_sha256_write_Num(&sha, pCtx->m_Txs.m_Krn.m_hMin);
	secp256k1_sha256_write_Num(&sha, pCtx->m_Txs.m_Krn.m_hMax);
	secp256k1_sha256_write_Num(&sha, pCtx->m_Txs.m_Krn.m_Fee);
	secp256k1_scalar_get_b32(hvOuter.m_pVal, &pCtx->m_p->u.m_TxBalance.m_sk);
	secp256k1_sha256_write_UintBig(&sha, &hvOuter);
	secp256k1_sha256_finalize(&sha, hvOuter.m_pVal);

	// derive keys
	KernelKeys keys;
	static const char szSalt[] = "hw-wlt-snd-sh";
	NonceGenerator ng;
	NonceGenerator_Init(&ng, szSalt, sizeof(szSalt), &hvOuter);
	NonceGenerator_NextScalar(&ng, &keys.m_kKrn);
	NonceGenerator_NextScalar(&ng, &keys.m_kNonce);
	SECURE_ERASE_OBJ(ng);

	if (bSplit)
		pCtx->m_Txs.m_pPeer = 0;
	else
	{
		// save Peer before generating output
		memcpy(hvPeer.m_pVal, pCtx->m_pIn->m_Mut.m_Peer.m_pVal, sizeof(hvPeer.m_pVal));
		pCtx->m_Txs.m_pPeer = &hvPeer;
	}

	KernelUpdateKeys(&pCtx->m_pOut->m_Tx.m_Comms, &keys, 0);
	TxKernel_getID_Ex(&pCtx->m_Txs.m_Krn, &pCtx->m_pOut->m_Tx.m_Comms, &hvOuter, &pCtx->m_hvKrn, 1);

	// all set
	pCtx->m_Txs.m_pKrnID = &hvOuter;

	uint16_t errCode = KeyKeeper_ConfirmSpend(pCtx->m_p, &pCtx->m_Txs);
	if (c_KeyKeeper_Status_Ok != errCode)
		return errCode;

	Kernel_SignPartial(&pCtx->m_pOut->m_Tx.m_TxSig.m_kSig, &pCtx->m_pOut->m_Tx.m_Comms, &hvOuter, &keys);

	secp256k1_scalar_add(&keys.m_kKrn, &keys.m_kKrn, &pCtx->m_skKrn);
	TxAggr_ToOffset(pCtx->m_p, &keys.m_kKrn, &pCtx->m_pOut->m_Tx);

	return c_KeyKeeper_Status_Ok;
}

PROTO_METHOD(TxSendShielded)
{
	PROTO_UNUSED_ARGS;

	if (nIn)
		return c_KeyKeeper_Status_ProtoError;

	TxSendShieldedContext ctx;
	ctx.m_p = p;
	ctx.m_pIn = pIn;
	ctx.m_pOut = pOut;
	ctx.m_pSh = &KeyKeeper_GetAuxBuf(p)->m_Sh;

	AddrID addrID;
	N2H_uint(addrID, pIn->m_Mut.m_AddrID, 64);

	uint16_t errCode = TxAggr_Get(p, &ctx.m_Txs, &pIn->m_Tx, 1);
	if (errCode)
		return errCode;

	if (!ctx.m_Txs.m_NetAmount)
		return MakeStatus(c_KeyKeeper_Status_Unspecified, 21); // not sending/splitting

	if (IsUintBigZero(&pIn->m_Mut.m_Peer))
		return MakeStatus(c_KeyKeeper_Status_UserAbort, 22); // conventional transfers must always be signed

	ctx.m_Txs.m_Flags = c_KeyKeeper_ConfirmSpend_Shielded;


	if (pIn->m_UsePublicGen)
	{
		if (!TxSendShielded_OfflineAddrCheck(&ctx))
			return MakeStatus(c_KeyKeeper_Status_Unspecified, 26);

		if (!TxSendShielded_OfflineMakeTicket(&ctx))
			return MakeStatus(c_KeyKeeper_Status_Unspecified, 27);

		ctx.m_Txs.m_Flags |= c_KeyKeeper_ConfirmSpend_Offline;
	}
	else
	{
		if (!TxSendShielded_VoucherCheck(&ctx))
			return MakeStatus(c_KeyKeeper_Status_Unspecified, 23);
	}

	if (addrID)
	{
		// should be a split tx
		DeriveAddress(p, addrID, &ctx.m_skKrn, &ctx.m_hvKrn);
		if (memcmp(ctx.m_hvKrn.m_pVal, pIn->m_Mut.m_Peer.m_pVal, sizeof(ctx.m_hvKrn.m_pVal)))
			return MakeStatus(c_KeyKeeper_Status_Unspecified, 24);

		ctx.m_Txs.m_Flags |= c_KeyKeeper_ConfirmSpend_Split;
	}

	errCode = TxSendShielded_VerifyParams(&ctx);
	if (errCode)
		return errCode;

	errCode = TxSendShielded_FinalyzeTx(&ctx, !!addrID);


	return errCode;
}
