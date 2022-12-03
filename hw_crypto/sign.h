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
	CompactPoint m_NoncePub;
	UintBig m_k; // scalar, but in a platform-independent way

} Signature; // Schnorr

void Signature_Sign(Signature*, const UintBig* pMsg, const secp256k1_scalar* pSk);
void Signature_SignPartial(Signature*, const UintBig* pMsg, const secp256k1_scalar* pSk, const secp256k1_scalar* pNonce);
int Signature_IsValid(const Signature*, const UintBig* pMsg, FlexPoint* pPk);
