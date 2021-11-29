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

#include "broadcast_msg_validator.h"
#include "utility/logger.h"
#include "wallet/core/common.h"

namespace beam::wallet
{
    bool BroadcastMsgValidator::stringToPublicKey(const std::string& keyHexString, PublicKey& out)
    {
        bool isKeyStringValid = true;
        ByteBuffer keyArray = from_hex(keyHexString, /*out*/ &isKeyStringValid);
        if (!isKeyStringValid)
        {
            return false;
        }
        
        size_t keySize = keyArray.size();
        assert(keySize <= static_cast<size_t>(UINT32_MAX));
        Blob keyBlob(keyArray.data(), static_cast<uint32_t>(keySize));

        out = PeerID(ECC::uintBig(keyBlob));
        return true;
    }

    void BroadcastMsgValidator::setPublisherKeys(const std::vector<PublicKey>& keys)
    {
        m_publisherKeys.clear();
        for (const auto& key : keys)
        {
            m_publisherKeys.push_back(key);
        }
    }

    // Later on can be implemented in ProtocolBase::VerifyMsg() and become incapsulated in BroadcatRouter class
    bool BroadcastMsgValidator::isSignatureValid(const BroadcastMsg& msg) const
    {
        SignatureHandler signValidator;

        try
        {
            fromByteBuffer(msg.m_signature, signValidator.m_Signature);
        }
        catch(...)
        {
            LOG_WARNING() << "broadcast message signature deserialization exception";
            return false;
        }
        signValidator.m_data = msg.m_content;

        auto it = std::find_if( std::cbegin(m_publisherKeys),
                                std::cend(m_publisherKeys),
                                [&signValidator](const PublicKey& pk)
                                {
                                    return signValidator.IsValid(pk);
                                });

        return it != std::cend(m_publisherKeys);
    }

} // namespace beam::wallet
