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

#define USE_BASIC_CONFIG

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wunused-function"
#else
#	pragma warning (push, 0) // suppress warnings from secp256k1
#	pragma warning (disable: 4706) // assignment within conditional expression
#endif

#include "secp256k1-zkp/src/basic-config.h"
#include "secp256k1-zkp/include/secp256k1.h"
#include "secp256k1-zkp/src/scalar.h"
#include "secp256k1-zkp/src/group.h"
#include "secp256k1-zkp/src/hash.h"

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic pop
#else
#	pragma warning (default: 4706)
#	pragma warning (pop)
#endif


static const uint8_t  ECC_Min_MultiMac_Prepared_nBits = 4;
static const uint8_t  ECC_Min_MultiMac_Prepared_nCount = 1 << (ECC_Min_MultiMac_Prepared_nBits - 1); // odd powers

struct ECC_Min_MultiMac_Prepared {
	secp256k1_ge_storage m_pPt[ECC_Min_MultiMac_Prepared_nCount]; // odd powers
};

#define ECC_Min_MultiMac_Directions 2 // must be 1 or 2. For 2 interleaving is used. Faster (~1 effective window bit), but needs an extra scalar per element

struct ECC_Min_MultiMac_WNaf_Cursor {
	uint8_t m_iBit;
	uint8_t m_iElement;
};

struct ECC_Min_MultiMac_WNaf {
	ECC_Min_MultiMac_WNaf_Cursor m_pC[ECC_Min_MultiMac_Directions];
};

struct ECC_Min_MultiMac_Scalar {
	secp256k1_scalar m_pK[ECC_Min_MultiMac_Directions];
};

struct ECC_Min_MultiMac_Context
{
	secp256k1_gej* m_pRes;

	unsigned int m_Count;
	const ECC_Min_MultiMac_Prepared* m_pPrep;
	ECC_Min_MultiMac_Scalar* m_pS;
	ECC_Min_MultiMac_WNaf* m_pWnaf;

};

void ECC_Min_MultiMac_Calculate(const ECC_Min_MultiMac_Context*);
