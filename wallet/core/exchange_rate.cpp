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

#include "exchange_rate.h"

namespace beam::wallet
{
    std::string ExchangeRate::to_string(const ExchangeRate::Currency& currency)
    {
        switch (currency)
        {
            case Currency::Beam:
                return std::string(beamCurrencyStr);
            case Currency::Bitcoin:
                return std::string(btcCurrencyStr);
            case Currency::Litecoin:
                return std::string(ltcCurrencyStr);
            case Currency::Qtum:
                return std::string(qtumCurrencyStr);
            case Currency::Usd:
                return std::string(usdCurrencyStr);
            default:
                return std::string(unknownCurrencyStr);
        }
    }

    ExchangeRate::Currency ExchangeRate::from_string(const std::string& c)
    {
        if (c == beamCurrencyStr)
            return Currency::Beam;
        else if (c == btcCurrencyStr)
            return Currency::Bitcoin;
        else if (c == ltcCurrencyStr)
            return Currency::Litecoin;
        else if (c == qtumCurrencyStr)
            return Currency::Qtum;
        else if (c == usdCurrencyStr)
            return Currency::Usd;
        else return Currency::Unknown;
    }

    bool ExchangeRate::operator==(const ExchangeRate& other) const
    {
        return m_currency == other.m_currency
            && m_unit == other.m_unit
            && m_rate == other.m_rate
            && m_updateTime == other.m_updateTime;
    }

    bool ExchangeRate::operator!=(const ExchangeRate& other) const
    {
        return !(*this == other);
    }

} // namespace beam::wallet
