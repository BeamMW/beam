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

#include "newscast_protocol_parser.h"
#include "utility/logger.h"
#include "wallet/core/common.h"

namespace beam::wallet
{
    boost::optional<NewscastProtocolParser::PublicKey> NewscastProtocolParser::stringToPublicKey(const std::string& keyHexString)
    {
        bool isKeyStringValid = true;
        ByteBuffer keyArray = from_hex(keyHexString, /*out*/ &isKeyStringValid);
        if (!isKeyStringValid)
        {
            return boost::none;
        }
        
        size_t keySize = keyArray.size();
        assert(keySize <= UINT32_MAX);
        Blob keyBlob(keyArray.data(), static_cast<uint32_t>(keySize));

        return ECC::uintBig(keyBlob);
    }

    void NewscastProtocolParser::setPublisherKeys(const std::vector<PublicKey>& keys)
    {
        m_publisherKeys.clear();
        for (const auto& key : keys)
        {
            m_publisherKeys.push_back(key);
        }
    }

    boost::optional<NewsMessage> NewscastProtocolParser::parseMessage(const ByteBuffer& msg) const
    {
        if (msg.empty() || msg.size() < MsgHeader::SIZE) return boost::none;
        
        NewsMessage freshNews;
        SignatureHandler signValidator;
        try
        {
            MsgHeader header(msg.data());
            if (header.V0 != 0 ||
                header.V1 != 0 ||
                header.V2 != m_protocolVersion ||
                header.type != MsgType)
            {
                LOG_WARNING() << "news message version unsupported";
                return boost::none;
            }

            // message body
            Deserializer d;
            d.reset(msg.data() + header.SIZE, header.size);
            d & freshNews;
            d & signValidator.m_Signature;
        }
        catch(...)
        {
            LOG_WARNING() << "news message deserialization exception";
            return boost::none;
        }

        signValidator.m_data = toByteBuffer(freshNews);
        auto it = std::find_if( std::cbegin(m_publisherKeys),
                                std::cend(m_publisherKeys),
                                [&signValidator](const PublicKey& pk)
                                {
                                    return signValidator.IsValid(pk);
                                });

        if (it != std::cend(m_publisherKeys))
            return freshNews;
        else
            return boost::none;
    }

} // namespace beam::wallet
