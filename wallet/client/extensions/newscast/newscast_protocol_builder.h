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

#include "news_message.h"
#include "core/ecc_native.h"

#include "boost/optional.hpp"

namespace beam::wallet
{
    /**
     *  Build newscast protocol messages.
     */
    class NewscastProtocolBuilder
    {
        using PrivateKey = ECC::Scalar::Native;

    public:
        /// Convert private key from HEX string representation to the internal type
        static boost::optional<PrivateKey> stringToPrivateKey(const std::string& keyHexString);

        /// Create message signed with private key
        static ByteBuffer createMessage(const NewsMessage& content, const PrivateKey& key);

    private:
        static constexpr uint8_t MsgType = 1;
        static constexpr uint8_t m_protocolVersion = 1;
    };

} // namespace beam::wallet
