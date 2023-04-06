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
#include "coinid.h"

typedef struct
{
	UintBig m_Secret;
	secp256k1_scalar m_kCoFactor;

} Kdf;

void Kdf_Init(Kdf*, const UintBig* pSeed);
void Kdf_Derive_PKey(const Kdf*, const UintBig* pHv, secp256k1_scalar* pK);
void Kdf_Derive_SKey(const Kdf*, const UintBig* pHv, secp256k1_scalar* pK);
void Kdf_getChild(Kdf*, uint32_t iChild, const Kdf* pParent);

void CoinID_getSkComm(const Kdf*, const CoinID*, secp256k1_scalar*, CompactPoint*);
