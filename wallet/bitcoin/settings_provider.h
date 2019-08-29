// Copyright 2019 The Beam Team
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

#include "core/serialization_adapters.h"
#include "utility/io/address.h"
#include "utility/common.h"
#include "wallet/common.h"
#include "wallet/wallet_db.h"
#include "settings.h"

namespace beam::bitcoin
{
    class IBitcoinCoreSettingsProvider
    {
    public:
        using Ptr = std::shared_ptr<IBitcoinCoreSettingsProvider>;

        virtual ~IBitcoinCoreSettingsProvider() = default;

        virtual BitcoinCoreSettings GetBitcoinCoreSettings() const = 0;
    };

    class IElectrumSettingsProvider
    {
    public:
        using Ptr = std::shared_ptr<IElectrumSettingsProvider>;

        virtual ~IElectrumSettingsProvider() = default;

        virtual ElectrumSettings GetElectrumSettings() const = 0;
    };

    class ISettingsProvider
        : public IBitcoinCoreSettingsProvider
        , public IElectrumSettingsProvider
    {
    public:
        using Ptr = std::shared_ptr<ISettingsProvider>;

        virtual Settings GetSettings() const = 0;
        virtual void SetSettings(const Settings& settings) = 0;
        virtual void ResetSettings() = 0;
    };

    class SettingsProvider
        : public ISettingsProvider
    {
    public:
        SettingsProvider(wallet::IWalletDB::Ptr walletDB);

        BitcoinCoreSettings GetBitcoinCoreSettings() const override;
        ElectrumSettings GetElectrumSettings() const override;
        Settings GetSettings() const override;
        void SetSettings(const Settings& settings) override;
        void ResetSettings() override;

        void Initialize();

    protected:

        virtual const char* GetSettingsName() const;
        virtual Settings GetEmptySettings();

    private:
        wallet::IWalletDB::Ptr m_walletDB;
        std::unique_ptr<Settings> m_settings;
    };
} // namespace beam::bitcoin