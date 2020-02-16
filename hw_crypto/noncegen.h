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

typedef struct tagECC_Min_NonceGenerator
{
	// RFC-5869
	uint8_t m_Prk[ECC_Min_nBytes];
	uint8_t m_Okm[ECC_Min_nBytes];

	uint8_t m_Counter; // wraps-around, it's fine
	uint8_t m_FirstTime;

} ECC_Min_NonceGenerator;

void ECC_Min_NonceGenerator_Init(ECC_Min_NonceGenerator*, const char* szSalt, size_t nSalt, const uint8_t* pSeed, size_t nSeed);
void ECC_Min_NonceGenerator_NextOkm(ECC_Min_NonceGenerator*);
void ECC_Min_NonceGenerator_NextScalar(ECC_Min_NonceGenerator*, secp256k1_scalar*);
