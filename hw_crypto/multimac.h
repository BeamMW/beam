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
#define BeamCrypto_MultiMac_Prepared_nBits 4
#define BeamCrypto_MultiMac_Prepared_nCount (1 << (BeamCrypto_MultiMac_Prepared_nBits - 1)) // odd powers

typedef struct tagBeamCrypto_MultiMac_Prepared {
	secp256k1_ge_storage m_pPt[BeamCrypto_MultiMac_Prepared_nCount]; // odd powers
} BeamCrypto_MultiMac_Prepared;


typedef struct tagBeamCrypto_MultiMac_WNaf_Cursor {
	uint8_t m_iBit;
	uint8_t m_iElement;
} BeamCrypto_MultiMac_WNaf_Cursor;

typedef struct tagBeamCrypto_MultiMac_WNaf {
	BeamCrypto_MultiMac_WNaf_Cursor m_pC[BeamCrypto_MultiMac_Directions];
} BeamCrypto_MultiMac_WNaf;

typedef struct tagBeamCrypto_MultiMac_Scalar {
	secp256k1_scalar m_pK[BeamCrypto_MultiMac_Directions];
} BeamCrypto_MultiMac_Scalar;

typedef struct tagBeamCrypto_MultiMac_Context
{
	secp256k1_gej* m_pRes;

	unsigned int m_Count;
	const BeamCrypto_MultiMac_Prepared* m_pPrep;
	BeamCrypto_MultiMac_Scalar* m_pS;
	BeamCrypto_MultiMac_WNaf* m_pWnaf;

} BeamCrypto_MultiMac_Context;

void BeamCrypto_MultiMac_Calculate(const BeamCrypto_MultiMac_Context*);
