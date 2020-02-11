// Copyright 2020 The Beam Team
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

#include "wallet/client/extensions/broadcast_gateway/interface.h"

#include "core/ecc_native.h"    // PrivateKey

namespace beam::wallet
{
    /**
     *  Creates broadcast messages.
     */
    class BroadcastMsgCreator
    {
        using PrivateKey = ECC::Scalar::Native;

    public:
        /// Convert private key from HEX string representation to the internal type
        static bool stringToPrivateKey(const std::string& keyHexString, PrivateKey& out);

        /// Create message signed with private key
        static BroadcastMsg createSignedMessage(const ByteBuffer& content, const PrivateKey& key);
    };

} // namespace beam::wallet
