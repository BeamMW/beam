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

    constexpr std::string_view beamCurrencyStr     = "beam";
    constexpr std::string_view btcCurrencyStr      = "btc";
    constexpr std::string_view ltcCurrencyStr      = "ltc";
    constexpr std::string_view qtumCurrencyStr     = "qtum";
    constexpr std::string_view usdCurrencyStr      = "usd";
    constexpr std::string_view dogeCurrencyStr =     "doge";
    constexpr std::string_view dashCurrencyStr =     "dash";
    constexpr std::string_view ethereumCurrencyStr = "ethereum";
    constexpr std::string_view daiCurrencyStr =      "dai";
    constexpr std::string_view usdtCurrencyStr =     "usdt";
    constexpr std::string_view wbtcCurrencyStr =     "wbtc";
    constexpr std::string_view bchCurrencyStr =      "bch";
    constexpr std::string_view unknownCurrencyStr  = "unknown";
    constexpr std::string_view exchangeRateOffStr  = "off";

    struct ExchangeRate
    {
        static const std::string USD;
        static const std::string BTC;
       /* enum class Currency : uint32_t
        {
            Beam,
            Bitcoin,
            Litecoin,
            Qtum,
            Usd,
            Dogecoin,
            Dash,
            Ethereum,
            Dai,
            Usdt,
            WBTC,
            Bitcoin_Cash,
            Unknown
        };
       */
    
        std::string m_from;
        std::string m_to;
        Amount      m_rate;
        Timestamp   m_updateTime;

        SERIALIZE(m_from, m_to, m_rate, m_updateTime);
        bool operator==(const ExchangeRate& other) const;
        bool operator!=(const ExchangeRate& other) const;
    };

    struct ExchangeRateHistoryEntity : public ExchangeRate
    {
        ExchangeRateHistoryEntity() = default;
        explicit ExchangeRateHistoryEntity(const ExchangeRate& rate) : ExchangeRate(rate) {}
        Height m_height = 0;
    };

} // namespace beam::wallet
