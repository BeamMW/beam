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

    constexpr std::string_view desktopAppStr = "desktop";
    constexpr std::string_view androidAppStr = "android";
    constexpr std::string_view iosAppStr = "ios";
    constexpr std::string_view unknownAppStr = "unknown";

    constexpr std::string_view beamCurrencyStr = "beam";
    constexpr std::string_view btcCurrencyStr = "btc";
    constexpr std::string_view ltcCurrencyStr = "ltc";
    constexpr std::string_view dCurrencyStr = "d";
    constexpr std::string_view qtumCurrencyStr = "qtum";
    constexpr std::string_view usdCurrencyStr = "usd";
    constexpr std::string_view unknownCurrencyStr = "unknown";

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

        static std::string to_string(Application a)
        {
            switch (a)
            {
                case Application::DesktopWallet:
                    return std::string(desktopAppStr);
                case Application::AndroidWallet:
                    return std::string(androidAppStr);
                case Application::IOSWallet:
                    return std::string(iosAppStr);
                default:
                    return std::string(unknownAppStr);
            }
        };

        static Application from_string(const std::string& type)
        {
            if (type == desktopAppStr)
                return Application::DesktopWallet;
            else if (type == androidAppStr)
                return Application::AndroidWallet;
            else if (type == iosAppStr)
                return Application::IOSWallet;
            else return Application::Unknown;
        };

        bool operator==(const VersionInfo& other) const
        {
            return m_application == other.m_application
                && m_version == other.m_version;
        };

        bool operator!=(const VersionInfo& other) const
        {
            return !(*this == other);
        };
    };

    struct ExchangeRate
    {
        enum class Currency : uint32_t
        {
            Beam,
            Bitcoin,
            Litecoin,
            Denarius,
            Qtum,
            Usd,
            Unknown
        };
    
        Currency m_currency;
        Currency m_unit;            // unit of m_rate measurment, e.g. USD or any other currency
        Amount m_rate;              // value as decimal fixed point. m_rate = 100,000,000 is 1 unit
        Timestamp m_updateTime;

        SERIALIZE(m_currency, m_unit, m_rate, m_updateTime);

        static std::string to_string(const Currency& currency)
        {
            switch (currency)
            {
                case Currency::Beam:
                    return std::string(beamCurrencyStr);
                case Currency::Bitcoin:
                    return std::string(btcCurrencyStr);
                case Currency::Litecoin:
                    return std::string(ltcCurrencyStr);
                case Currency::Denarius:
                    return std::string(dCurrencyStr);
                case Currency::Qtum:
                    return std::string(qtumCurrencyStr);
                case Currency::Usd:
                    return std::string(usdCurrencyStr);
                default:
                    return std::string(unknownCurrencyStr);
            }
        };

        static Currency from_string(const std::string& c)
        {
            if (c == beamCurrencyStr)
                return Currency::Beam;
            else if (c == btcCurrencyStr)
                return Currency::Bitcoin;
            else if (c == ltcCurrencyStr)
                return Currency::Litecoin;
            else if (c == dCurrencyStr)
                return Currency::Denarius;
            else if (c == qtumCurrencyStr)
                return Currency::Qtum;
            else if (c == usdCurrencyStr)
                return Currency::Usd;
            else return Currency::Unknown;
        };

        bool operator==(const ExchangeRate& other) const
        {
            return m_currency == other.m_currency
                && m_unit == other.m_unit
                && m_rate == other.m_rate
                && m_updateTime == other.m_updateTime;
        };
        bool operator!=(const ExchangeRate& other) const
        {
            return !(*this == other);
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
        virtual void onExchangeRates(const std::vector<ExchangeRate>&) = 0;
    };

} // namespace beam::wallet
