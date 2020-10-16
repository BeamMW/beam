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

#define BeamCrypto_MultiMac_Directions 2 // must be 1 or 2. For 2 interleaving is used. Faster (~1 effective window bit), but needs an extra scalar per element
#define BeamCrypto_MultiMac_Fast_nBits 4
#define BeamCrypto_MultiMac_Secure_nBits 4
#define BeamCrypto_MultiMac_Fast_nCount (1 << (BeamCrypto_MultiMac_Fast_nBits - 1)) // odd powers
#define BeamCrypto_MultiMac_Secure_nCount (1 << BeamCrypto_MultiMac_Secure_nBits)

typedef struct {
	secp256k1_ge_storage m_pPt[BeamCrypto_MultiMac_Fast_nCount]; // odd powers
} BeamCrypto_MultiMac_Fast;

typedef struct {
	secp256k1_ge_storage m_pPt[BeamCrypto_MultiMac_Secure_nCount + 1]; // the last is the compensation term
} BeamCrypto_MultiMac_Secure;

typedef struct {
	uint8_t m_iBit;
	uint8_t m_iElement;
} BeamCrypto_MultiMac_WNaf_Cursor;

typedef struct {
	BeamCrypto_MultiMac_WNaf_Cursor m_pC[BeamCrypto_MultiMac_Directions];
} BeamCrypto_MultiMac_WNaf;

typedef struct {
	secp256k1_scalar m_pK[BeamCrypto_MultiMac_Directions];
} BeamCrypto_MultiMac_Scalar;

typedef struct
{
	secp256k1_gej* m_pRes;

	unsigned int m_Fast;
	unsigned int m_Secure;

	const BeamCrypto_MultiMac_Fast* m_pGenFast;
	BeamCrypto_MultiMac_Scalar* m_pS;
	BeamCrypto_MultiMac_WNaf* m_pWnaf;

	const BeamCrypto_MultiMac_Secure* m_pGenSecure;
	const secp256k1_scalar* m_pSecureK;

	secp256k1_fe* m_pZDenom; // optional common z-denominator of 'fast' generators.

} BeamCrypto_MultiMac_Context;

void BeamCrypto_MultiMac_Calculate(const BeamCrypto_MultiMac_Context*);

#define BeamCrypto_MultiMac_Fast_nGenerators (sizeof(uint64_t) * 8 * 2)
#define BeamCrypto_MultiMac_Fast_Idx_H BeamCrypto_MultiMac_Fast_nGenerators

typedef struct
{
	BeamCrypto_MultiMac_Fast m_pGenFast[BeamCrypto_MultiMac_Fast_Idx_H + 1];
	BeamCrypto_MultiMac_Secure m_pGenGJ[2];

} BeamCrypto_Context;

BeamCrypto_Context* BeamCrypto_Context_get();

// simplified versions
void BeamCrypto_MulPoint(BeamCrypto_FlexPoint*, const BeamCrypto_MultiMac_Secure*, const secp256k1_scalar*);
void BeamCrypto_MulG(BeamCrypto_FlexPoint*, const secp256k1_scalar*);
void BeamCrypto_Sk2Pk(BeamCrypto_UintBig*, secp256k1_scalar*);
