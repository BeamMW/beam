// Copyright 2019 The Beam Team
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

#include "core/block_rw.h"
#include "wallet/core/private_key_keeper.h"

namespace beam::wallet
{
    namespace JsonFields
    {
        inline const char* Status = "status";
        inline const char* Count = "count";
        inline const char* Result = "result";
        inline const char* Offset = "offset";
        inline const char* PaymentProofSig = "payment_proof_sig";
        inline const char* UserAgreement = "agreement";
        inline const char* Kernel = "kernel";
        inline const char* PublicKdf = "pub_kdf";
        inline const char* PublicNonce = "pub_nonce";
        inline const char* Commitment = "commitment";
    }
};
