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
    /*std::string ExchangeRate::to_string(const ExchangeRate::Currency& currency)
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
            case Currency::Dogecoin:
                return std::string(dogeCurrencyStr);
            case Currency::Dash:
                return std::string(dashCurrencyStr);
            case Currency::Ethereum:
                return std::string(ethereumCurrencyStr);
            case Currency::Dai:
                return std::string(daiCurrencyStr);
            case Currency::Usdt:
                return std::string(usdtCurrencyStr);
            case Currency::WBTC:
                return std::string(wbtcCurrencyStr);
#if defined(BITCOIN_CASH_SUPPORT)
            case Currency::Bitcoin_Cash:
                return std::string(bchCurrencyStr);
#endif // BITCOIN_CASH_SUPPORT
            default:
                return std::string(unknownCurrencyStr);
        }
    }*/

    /*ExchangeRate::Currency ExchangeRate::from_string(const std::string& c)
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
        else if (c == ethereumCurrencyStr)
            return Currency::Ethereum;
        else if (c == dogeCurrencyStr)
            return Currency::Dogecoin;
        else if (c == dashCurrencyStr)
            return Currency::Dash;
        else if (c == daiCurrencyStr)
            return Currency::Dai;
        else if (c == usdtCurrencyStr)
            return Currency::Usdt;
        else if (c == wbtcCurrencyStr)
            return Currency::WBTC;
#if defined(BITCOIN_CASH_SUPPORT)
        else if (c == bchCurrencyStr)
            return Currency::Bitcoin_Cash;
#endif // BITCOIN_CASH_SUPPORT
        else return Currency::Unknown;
    }*/

    bool ExchangeRate::operator==(const ExchangeRate& other) const
    {
        return m_from == other.m_from
            && m_to == other.m_to
            && m_rate == other.m_rate
            && m_updateTime == other.m_updateTime;
    }

    bool ExchangeRate::operator!=(const ExchangeRate& other) const
    {
        return !(*this == other);
    }

    const std::string ExchangeRate::USD = std::string("USD");
    const std::string ExchangeRate::BTC = std::string("BTC");
} // namespace beam::wallet
