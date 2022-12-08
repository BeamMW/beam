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

#define c_MultiMac_Fast_nBits 4 // effectively it'd be 1 bit more, because we both add and subtract (i.e. interleave)
#define c_MultiMac_Secure_nBits 4
#define c_MultiMac_Fast_nCount (1 << (c_MultiMac_Fast_nBits - 1)) // odd powers
#define c_MultiMac_Secure_nCount (1 << c_MultiMac_Secure_nBits)

typedef struct {
	secp256k1_ge_storage m_pPt[c_MultiMac_Fast_nCount]; // odd powers
} MultiMac_Fast;

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

	unsigned int m_Fast;
	unsigned int m_Secure;

	const MultiMac_Fast* m_pGenFast;
	secp256k1_scalar* m_pFastK; // would be modified during calculation, value won't be preserved
	MultiMac_WNaf* m_pWnaf;

	const MultiMac_Secure* m_pGenSecure;
	const secp256k1_scalar* m_pSecureK;

	const secp256k1_fe* m_pZDenom; // optional common z-denominator of 'fast' generators.

} MultiMac_Context;

void MultiMac_Calculate(const MultiMac_Context*);

#define c_MultiMac_Fast_nGenerators (sizeof(uint64_t) * 8 * 2)
#define c_MultiMac_Fast_Idx_H c_MultiMac_Fast_nGenerators

typedef struct
{
	MultiMac_Fast m_pGenFast[c_MultiMac_Fast_Idx_H + 1];
	MultiMac_Secure m_pGenGJ[2];

} Context;

Context* Context_get();

// simplified versions
void MulPoint(FlexPoint*, const MultiMac_Secure*, const secp256k1_scalar*);
void MulG(FlexPoint*, const secp256k1_scalar*);
void Sk2Pk(UintBig*, secp256k1_scalar*);
