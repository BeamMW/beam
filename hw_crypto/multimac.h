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

#pragma once
#include "ecc_decl.h"

#ifdef BeamCrypto_SlowLoad
#	define c_MultiMac_nBits_Rangeproof 2
#else // BeamCrypto_SlowLoad
#	define c_MultiMac_nBits_Rangeproof 4
#endif // BeamCrypto_SlowLoad

#define c_MultiMac_nBits_H 4
#define c_MultiMac_nBits_Secure 4
#define c_MultiMac_Secure_nCount (1 << c_MultiMac_nBits_Secure)

#ifdef BeamCrypto_ScarceStack
#	define c_MultiMac_nBits_Custom 3
#else
#	define c_MultiMac_nBits_Custom 4
#endif // BeamCrypto_ScarceStack

#define c_MultiMac_OddCount(numBits) (1 << ((numBits) - 1))


typedef struct {
	secp256k1_ge_storage m_pPt[c_MultiMac_Secure_nCount + 1]; // the last is the compensation term
} MultiMac_Secure;

typedef struct {
	uint8_t m_iBit;
	uint8_t m_iElement;
} MultiMac_WNaf;

typedef struct
{
	secp256k1_gej* m_pRes;

	struct
	{
		unsigned int m_Count;
		unsigned int m_WndBits;
		const secp256k1_ge_storage* m_pGen0;
		secp256k1_scalar* m_pK; // would be modified during calculation, value won't be preserved
		MultiMac_WNaf* m_pWnaf;
		const secp256k1_fe* m_pZDenom; // optional common z-denominator of 'fast' custom generators.

	} m_Fast;

	struct
	{
		unsigned int m_Count;
		const MultiMac_Secure* m_pGen;
		const secp256k1_scalar* m_pK;

	} m_Secure;

} MultiMac_Context;

void MultiMac_Calculate(MultiMac_Context*);

#define c_MultiMac_Fast_nGenerators (sizeof(uint64_t) * 8 * 2)

typedef struct
{
	secp256k1_ge_storage m_pGenRangeproof[c_MultiMac_Fast_nGenerators][c_MultiMac_OddCount(c_MultiMac_nBits_Rangeproof)];
	secp256k1_ge_storage m_pGenH[c_MultiMac_OddCount(c_MultiMac_nBits_H)];
	MultiMac_Secure m_pGenGJ[2];

} Context;

Context* Context_get();

// simplified versions
void MulPoint(secp256k1_gej*, const MultiMac_Secure*, const secp256k1_scalar*);
void MulG(secp256k1_gej*, const secp256k1_scalar*);
void Sk2Pk(UintBig*, secp256k1_scalar*);
