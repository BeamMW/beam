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

typedef struct tagECC_Min_Oracle {
	secp256k1_sha256_t m_sha;
} ECC_Min_Oracle;

void ECC_Min_Oracle_Init(ECC_Min_Oracle*);
void ECC_Min_Oracle_Expose(ECC_Min_Oracle*, const uint8_t*, size_t);
void ECC_Min_Oracle_NextHash(ECC_Min_Oracle*, uint8_t*);
void ECC_Min_Oracle_NextScalar(ECC_Min_Oracle*, secp256k1_scalar*);
