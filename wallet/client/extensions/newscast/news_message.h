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

#include "utility/serialize.h"

#include "utility/common.h"
#include "wallet/core/common.h"

namespace beam::wallet
{
    /**
     *  Data object broadcasted using news channels
     */
    struct NewsMessage
    {
        enum class Type : uint32_t
        {
            WalletUpdateNotification,
            ExchangeRates,
            Unknown
        };

        Type m_type = Type::Unknown;
        ByteBuffer m_content;

        SERIALIZE(m_type, m_content);

        bool operator==(const NewsMessage& other) const
        {
            return m_type == other.m_type
                && m_content == other.m_content;
        };
        
        bool operator!=(const NewsMessage& other) const
        {
            return !(*this == other);
        };
    };

    struct ExchangeRates
    {
        Timestamp m_time;
        // std::vector<curr,rateDecimal>
    };

    class NewsMessageHandler
    {
    public:

        static std::string extractUpdateVersion(const NewsMessage& msg)
        {
            if (msg.m_type == NewsMessage::Type::WalletUpdateNotification)
            {
                std::string res;
                if (fromByteBuffer(msg.m_content, res)) return res;
            }
            return std::string();
        };

        static NewsMessage packUpdateVersion(std::string newUpdateVersion)
        {
            return NewsMessage {
                    NewsMessage::Type::WalletUpdateNotification,
                    toByteBuffer(newUpdateVersion)
                };
        };
    };
} // namespace beam::wallet
