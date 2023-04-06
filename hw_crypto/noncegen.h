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

typedef struct
{
	// RFC-5869
	UintBig m_Prk;
	UintBig m_Okm;

	uint8_t m_Counter; // wraps-around, it's fine
	uint8_t m_FirstTime;

	const uint8_t* m_pContext;
	size_t m_nContext;

} NonceGenerator;

void NonceGenerator_Init(NonceGenerator*, const char* szSalt, size_t nSalt, const UintBig* pSeed);
void NonceGenerator_NextOkm(NonceGenerator*);
void NonceGenerator_NextScalar(NonceGenerator*, secp256k1_scalar*);
