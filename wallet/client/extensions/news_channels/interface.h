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
            IOSWallet,
            Unknown
        };

        Application m_application;
        Version m_version;

        SERIALIZE(m_application, m_version);

        // TODO move string literals to one definition
        static std::string to_string(Application a)
        {
            switch (a)
            {
                case Application::DesktopWallet:
                    return "desktop";
                case Application::AndroidWallet:
                    return "android";
                case Application::IOSWallet:
                    return "ios";
                default:
                    return "unknown";
            }
        };

        static Application from_string(const std::string& type)
        {
            if (type == "desktop")
                return Application::DesktopWallet;
            else if (type == "android")
                return Application::AndroidWallet;
            else if (type == "ios")
                return Application::IOSWallet;
            else return Application::Unknown;
        };

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
            Usd,
            Unknown
        };

        struct ExchangeRate
        {
            Currency m_currency;
            Amount m_rate;
            Currency m_unit;    // unit of m_rate measurment, e.g. usd

            SERIALIZE(m_currency, m_rate, m_unit);
        };

        Timestamp m_ts;
        std::vector<ExchangeRate> m_rates;

        SERIALIZE(m_ts, m_rates);

        static std::string to_string(const Currency& currency)
        {
            switch (currency)
            {
                case Currency::Beam:
                    return "beam";
                case Currency::Bitcoin:
                    return "btc";
                case Currency::Litecoin:
                    return "ltc";
                case Currency::Qtum:
                    return "qtum";
                case Currency::Usd:
                    return "usd";
                default:
                    return "unknown";
            }
        };

        static Currency from_string(const std::string& c)
        {
            if (c == "beam")
                return Currency::Beam;
            else if (c == "btc")
                return Currency::Bitcoin;
            else if (c == "ltc")
                return Currency::Litecoin;
            else if (c == "qtum")
                return Currency::Qtum;
            else if (c == "usd")
                return Currency::Usd;
            else return Currency::Unknown;
        };
    };
    
    /**
     *  Interface for news channels observers. 
     */
    struct INewsObserver
    {
        virtual void onNewWalletVersion(const VersionInfo&, const ECC::uintBig&) = 0;
        // virtual void onBeamNews() = 0;
    };

    /**
     *  Interface for exchange rates observers.
     */
    struct IExchangeRateObserver
    {
        virtual void onExchangeRates(const ExchangeRates&) = 0;
    };

} // namespace beam::wallet
