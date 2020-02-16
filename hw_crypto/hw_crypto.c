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
static_assert(BeamCrypto_MultiMac_Prepared_nCount < s_WNaf_HiBit, "");

#ifdef USE_SCALAR_4X64
typedef uint64_t secp256k1_scalar_uint;
#else // USE_SCALAR_4X64
typedef uint32_t secp256k1_scalar_uint;
#endif // USE_SCALAR_4X64

typedef struct
{
	int m_Word;
	secp256k1_scalar_uint m_Msk;
} BitWalker;

inline static void BitWalker_SetPos(BitWalker* p, uint8_t iBit)
{
	const unsigned int nWordBits = sizeof(secp256k1_scalar_uint) * 8;

	p->m_Word = iBit / nWordBits;
	p->m_Msk = ((secp256k1_scalar_uint) 1) << (iBit & (nWordBits - 1));
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
		p->m_Msk = ((secp256k1_scalar_uint) 1) << (nWordBits - 1);

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

	uint8_t nWndBits = BeamCrypto_MultiMac_Prepared_nBits - 1;
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
	static_assert(BeamCrypto_MultiMac_Directions == 1);
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
			if (iBit >= BeamCrypto_nBits - BeamCrypto_MultiMac_Prepared_nBits)
				return 0;

			if (BitWalker_get(&bw, p->m_pK))
				break;

			iBit++;
			BitWalker_MoveUp(&bw);
		}

		BitWalker bw0 = bw;

		iBit += BeamCrypto_MultiMac_Prepared_nBits;
		for (uint32_t i = 0; i < BeamCrypto_MultiMac_Prepared_nBits; i++)
			BitWalker_MoveUp(&bw); // akward

		if (!BitWalker_get(&bw, p->m_pK))
			continue; // interleaving is not needed

		// set negative bits
		BitWalker_xor(&bw0, p->m_pK);
		BitWalker_xor(&bw0, p->m_pK + 1);

		for (uint8_t i = 1; i < BeamCrypto_MultiMac_Prepared_nBits; i++)
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

void BeamCrypto_MultiMac_Calculate(const BeamCrypto_MultiMac_Context* p)
{
	secp256k1_gej_set_infinity(p->m_pRes);

	for (unsigned int i = 0; i < p->m_Count; i++)
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
		
	for (uint16_t iBit = BeamCrypto_nBits + 1; iBit--; ) // extra bit may be necessary because of interleaving
	{
		if (!secp256k1_gej_is_infinity(p->m_pRes))
			secp256k1_gej_double_var(p->m_pRes, p->m_pRes, 0);

		for (unsigned int i = 0; i < p->m_Count; i++)
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

				secp256k1_ge ge;
				secp256k1_ge_from_storage(&ge, p->m_pPrep[i].m_pPt + (pWc->m_iElement & ~s_WNaf_HiBit));

				if (j)
					secp256k1_ge_neg(&ge, &ge);

				secp256k1_gej_add_ge_var(p->m_pRes, p->m_pRes, &ge, 0);

				if (iBit)
					WNaf_Cursor_MoveNext(pWc, p->m_pS[i].m_pK + j);
			}
		}
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

//////////////////////////////
// CoinID
#define BeamCrypto_CoinID_nSubkeyBits 24

int BeamCrypto_CoinID_getSchemeAndSubkey(const BeamCrypto_CoinID* p, BeamCrypto_CoinID_Scheme* pScheme, BeamCrypto_CoinID_SubKey* pSubkey)
{
	*pScheme = (BeamCrypto_CoinID_Scheme) (p->m_SubIdx >> BeamCrypto_CoinID_nSubkeyBits);
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

void BeamCrypto_CoinID_getHash(const BeamCrypto_CoinID* p, BeamCrypto_UintBig* pHash)
{
	secp256k1_sha256_t sha;
	secp256k1_sha256_initialize(&sha);

	BeamCrypto_CoinID_Scheme nScheme;
	BeamCrypto_CoinID_SubKey nSubkey;

	BeamCrypto_CoinID_getSchemeAndSubkey(p, &nScheme, &nSubkey);

	BeamCrypto_CoinID_SubKey nSubIdx = p->m_SubIdx;

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

