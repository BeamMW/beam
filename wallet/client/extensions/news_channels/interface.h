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

#include "wallet/core/common.h" // Version

namespace beam::wallet
{
    using namespace beam;
    
    struct VersionInfo
    {
        enum class Application : uint32_t
        {
            DesktopWallet,
            AndroidWallet,
            IOSWallet
        } m_application;
        Version m_version;

        SERIALIZE(m_application, m_version);

        bool operator==(const VersionInfo& o) const
        {
            return m_application == o.m_application
                && m_version == o.m_version;
        };
        bool operator!=(const VersionInfo& o) const
        {
            return !(*this == o);
        };
    };

    struct ExchangeRates
    {
        enum class Currency : uint32_t
        {
            Beam,
            Bitcoin,
            Litecoin,
            Qtum,
            Usd
        };
        struct ExchangeRate
        {
            Currency m_currency;
            Amount m_rate;
            Currency m_unit;    // unit of m_rate
        };

        Timestamp m_ts;
        std::vector<ExchangeRate> m_rates;
    };
    
    /**
     *  Interface for news channels observers. 
     */
    struct INewsObserver
    {
        virtual void onNewWalletVersion(const VersionInfo&) = 0;
        virtual void onExchangeRates(const ExchangeRates&) = 0;
    };

} // namespace beam::wallet
