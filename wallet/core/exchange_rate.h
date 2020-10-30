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

#include "utility/common.h"
#include "utility/serialize_fwd.h"

namespace beam::wallet
{
    using namespace beam;

    constexpr std::string_view beamCurrencyStr =    "beam";
    constexpr std::string_view btcCurrencyStr =     "btc";
    constexpr std::string_view ltcCurrencyStr =     "ltc";
    constexpr std::string_view qtumCurrencyStr =    "qtum";
    constexpr std::string_view usdCurrencyStr =     "usd";
    constexpr std::string_view unknownCurrencyStr = "unknown";

    constexpr std::string_view noSecondCurrencyStr = "off";

    struct ExchangeRate
    {
        enum class Currency : uint32_t
        {
            Beam,
            Bitcoin,
            Litecoin,
            Qtum,
            Usd,
            Unknown
        };
    
        Currency m_currency;
        Currency m_unit;            // unit of m_rate measurment, e.g. USD or any other currency
        Amount m_rate;              // value as decimal fixed point. m_rate = 100,000,000 is 1 unit
        Timestamp m_updateTime;

        SERIALIZE(m_currency, m_unit, m_rate, m_updateTime);

        static std::string to_string(const Currency&);
        static Currency from_string(const std::string&);

        bool operator==(const ExchangeRate& other) const;
        bool operator!=(const ExchangeRate& other) const;
    };

    struct ExchangeRateHistoryEntity : public ExchangeRate
    {
        ExchangeRateHistoryEntity() = default;
        ExchangeRateHistoryEntity(const ExchangeRate& rate) : ExchangeRate(rate) {}
        Height m_height = 0;
    };

} // namespace beam::wallet
