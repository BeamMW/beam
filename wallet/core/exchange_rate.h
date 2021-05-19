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
#include "currency.h"

namespace beam::wallet
{
    // This is old fork2 version, do not use it
    // Will be deprecated after F3 and removed in 6.1
    struct ExchangeRateF2
    {
        enum class CurrencyF2 : uint32_t
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

        CurrencyF2 m_currency;
        CurrencyF2 m_unit;
        Amount     m_rate;
        Timestamp  m_updateTime;

        SERIALIZE(m_currency, m_unit, m_rate, m_updateTime);
    };

    struct ExchangeRate
    {
        Currency    m_from = Currency::UNKNOWN();
        Currency    m_to   = Currency::UNKNOWN();
        Amount      m_rate = 0;
        Timestamp   m_updateTime = 0;

        SERIALIZE(m_from, m_to, m_rate, m_updateTime);
        bool operator==(const ExchangeRate& other) const;
        bool operator!=(const ExchangeRate& other) const;

        static ExchangeRate FromERH2(const ExchangeRateF2& r2);
    };

    typedef std::vector<ExchangeRate> ExchangeRates;
    struct ExchangeRateAtPoint: public ExchangeRate
    {
        explicit ExchangeRateAtPoint(const ExchangeRate& rate = ExchangeRate(), Height h = 0)
            : ExchangeRate(rate)
            , m_height(h)
        {
        }
        Height m_height = 0;
    };

    typedef std::vector<ExchangeRateAtPoint> ExchangeRatesHistory;
}
