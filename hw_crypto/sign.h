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
	BeamCrypto_CompactPoint m_NoncePub;
	BeamCrypto_UintBig m_k; // scalar, but in a platform-independent way

} BeamCrypto_Signature; // Schnorr

void BeamCrypto_Signature_Sign(BeamCrypto_Signature*, const BeamCrypto_UintBig* pMsg, const secp256k1_scalar* pSk);
void BeamCrypto_Signature_SignPartial(BeamCrypto_Signature*, const BeamCrypto_UintBig* pMsg, const secp256k1_scalar* pSk, const secp256k1_scalar* pNonce);
int BeamCrypto_Signature_IsValid(const BeamCrypto_Signature*, const BeamCrypto_UintBig* pMsg, BeamCrypto_FlexPoint* pPk);
