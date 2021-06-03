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
    bool ExchangeRate::operator==(const ExchangeRate &other) const
    {
        return m_from == other.m_from
               && m_to == other.m_to
               && m_rate == other.m_rate
               && m_updateTime == other.m_updateTime;
    }

    bool ExchangeRate::operator!=(const ExchangeRate &other) const
    {
        return !(*this == other);
    }

    ExchangeRate ExchangeRate::FromERH2(const ExchangeRateF2& rold)
    {
        ExchangeRate nrate;
        nrate.m_rate = rold.m_rate;
        nrate.m_updateTime = rold.m_updateTime;

        switch (rold.m_currency)
        {
            case ExchangeRateF2::CurrencyF2::Beam: nrate.m_from = Currency::BEAM(); break;
            case ExchangeRateF2::CurrencyF2::Bitcoin: nrate.m_from = Currency::BTC(); break;
            case ExchangeRateF2::CurrencyF2::Litecoin: nrate.m_from = Currency::LTC(); break;
            case ExchangeRateF2::CurrencyF2::Qtum: nrate.m_from = Currency::QTUM(); break;
            case ExchangeRateF2::CurrencyF2::Usd: nrate.m_from = Currency::USD(); break;
            case ExchangeRateF2::CurrencyF2::Dogecoin: nrate.m_from = Currency::DOGE(); break;
            case ExchangeRateF2::CurrencyF2::Dash: nrate.m_from = Currency::DASH(); break;
            case ExchangeRateF2::CurrencyF2::Ethereum: nrate.m_from = Currency::ETH(); break;
            case ExchangeRateF2::CurrencyF2::Dai: nrate.m_from = Currency::DAI(); break;
            case ExchangeRateF2::CurrencyF2::Usdt: nrate.m_from = Currency::USDT(); break;
            case ExchangeRateF2::CurrencyF2::WBTC: nrate.m_from = Currency::WBTC(); break;
            case ExchangeRateF2::CurrencyF2::Bitcoin_Cash: nrate.m_from = Currency::BCH(); break;
            default: {
                assert(false);
                nrate.m_from = Currency::UNKNOWN();
            }
        }

        switch(rold.m_unit)
        {
            case ExchangeRateF2::CurrencyF2::Usd: nrate.m_to = Currency::USD();  break;
            case ExchangeRateF2::CurrencyF2::Bitcoin: nrate.m_to = Currency::BTC(); break;
            default: {
                assert(false);
                nrate.m_from = Currency::UNKNOWN();
            }
        }

        return nrate;
    }
}
