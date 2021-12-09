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

#include "core/block_crypt.h"   // PeerID

namespace beam::wallet
{
    /**
     *  Validate message signatures according to publisher keys.
     */
    class BroadcastMsgValidator
    {
        using PublicKey = PeerID;

    public:
        BroadcastMsgValidator() {};

        void setPublisherKeys(const std::vector<PublicKey>& keys);

        bool isSignatureValid(const BroadcastMsg& msg) const;

        /// Convert public key from HEX string representation to the internal type
        static bool stringToPublicKey(const std::string& keyHexString, PublicKey& out);

    private:
        std::vector<PublicKey> m_publisherKeys;       /// publisher keys to validate messages
    };

} // namespace beam::wallet
