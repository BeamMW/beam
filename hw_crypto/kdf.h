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
	BeamCrypto_UintBig m_Secret;
	secp256k1_scalar m_kCoFactor;

} BeamCrypto_Kdf;

void BeamCrypto_Kdf_Init(BeamCrypto_Kdf*, const BeamCrypto_UintBig* pSeed);
void BeamCrypto_Kdf_Derive_PKey(const BeamCrypto_Kdf*, const BeamCrypto_UintBig* pHv, secp256k1_scalar* pK);
void BeamCrypto_Kdf_Derive_SKey(const BeamCrypto_Kdf*, const BeamCrypto_UintBig* pHv, secp256k1_scalar* pK);
void BeamCrypto_Kdf_getChild(BeamCrypto_Kdf*, uint32_t iChild, const BeamCrypto_Kdf* pParent);

void BeamCrypto_CoinID_getSk(const BeamCrypto_Kdf*, const BeamCrypto_CoinID*, secp256k1_scalar*);
void BeamCrypto_CoinID_getSkComm(const BeamCrypto_Kdf*, const BeamCrypto_CoinID*, secp256k1_scalar*, BeamCrypto_CompactPoint*);
