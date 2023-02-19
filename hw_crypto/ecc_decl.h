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

#define c_ECC_nBytes sizeof(secp256k1_scalar)
#define c_ECC_nBits (c_ECC_nBytes * 8)

typedef struct
{
	uint8_t m_pVal[c_ECC_nBytes];
} UintBig;

typedef struct
{
	// platform-independent EC point representation
	UintBig m_X;
	uint8_t m_Y;
} CompactPoint;


typedef struct
{
	UintBig m_X;
	UintBig m_Y;
} AffinePoint;

int Point_Ge_from_Compact(secp256k1_ge*, const CompactPoint*);
int Point_Ge_from_CompactNnz(secp256k1_ge*, const CompactPoint*);
void Point_Compact_from_Ge(CompactPoint*, const secp256k1_ge*);
uint8_t Point_Compact_from_Ge_Ex(UintBig* pX, const secp256k1_ge*);

#ifdef BeamCrypto_ExternalGej

uint32_t GejExt_Create();
void GejExt_Copy(uint32_t, uint32_t hSrc);
void GejExt_Destroy(uint32_t);

int GejExt_Add(uint32_t, uint32_t, uint32_t);
int GejExt_Mul(uint32_t, uint32_t, const secp256k1_scalar*, int bFast);
void GejExt_Set(uint32_t, const AffinePoint*);
void GejExt_Get(uint32_t, AffinePoint*);

#endif // BeamCrypto_ExternalGej
