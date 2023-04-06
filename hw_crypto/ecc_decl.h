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

typedef struct {
	secp256k1_gej m_Val;
} gej_t;

void Gej_Init(gej_t*);
void Gej_Destroy(gej_t*);
int Gej_Is_infinity(const gej_t* p);
void Gej_Add(gej_t* p, const gej_t* a, const gej_t* b);
void Gej_Mul_Ub(gej_t* p, const gej_t* a, const UintBig* k, int bFast);
void Gej_Mul2_Fast(gej_t* p, const gej_t* a, const UintBig* ka, const gej_t* b, const UintBig* kb);
void Gej_Set_Affine(gej_t* p, const AffinePoint* pAp);
void Gej_Get_Affine(const gej_t* p, AffinePoint* pAp);


#endif // BeamCrypto_ExternalGej
