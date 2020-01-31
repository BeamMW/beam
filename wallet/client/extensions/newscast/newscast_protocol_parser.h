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

#include "news_message.h"
#include "core/block_crypt.h"

#include "boost/optional.hpp"

namespace beam::wallet
{
    /**
     *  Validate message signatures according to publisher keys.
     */
    class NewscastProtocolParser
    {
        using PublicKey = PeerID;

    public:
        NewscastProtocolParser() {};

        void setPublisherKeys(const std::vector<PublicKey>& keys);
        boost::optional<NewsMessage> parseMessage(const ByteBuffer&) const;

        /// Convert public key from HEX string representation to the internal type
        static boost::optional<PublicKey> stringToPublicKey(const std::string& keyHexString);

    private:
        std::vector<PeerID> m_publisherKeys;       /// publisher keys to validate messages
    };

} // namespace beam::wallet
