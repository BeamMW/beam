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

#include "settings_provider.h"

namespace beam::bitcoin
{
    SettingsProvider::SettingsProvider(wallet::IWalletDB::Ptr walletDB)
        : m_walletDB(walletDB)
    {
    }

    BitcoinCoreSettings SettingsProvider::GetBitcoinCoreSettings() const
    {
        assert(m_settings);
        return m_settings->GetConnectionOptions();
    }

    ElectrumSettings SettingsProvider::GetElectrumSettings() const
    {
        assert(m_settings);
        return m_settings->GetElectrumConnectionOptions();
    }

    Settings SettingsProvider::GetSettings() const
    {
        assert(m_settings);
        return *m_settings;
    }

    void SettingsProvider::SetSettings(const Settings& settings)
    {
        // store to DB
        auto buffer = wallet::toByteBuffer(settings);
        m_walletDB->setVarRaw(GetSettingsName(), buffer.data(), static_cast<int>(buffer.size()));

        // update m_settings
        m_settings = std::make_unique<Settings>(settings);
    }

    void SettingsProvider::ResetSettings()
    {
        // remove from DB
        m_walletDB->removeVarRaw(GetSettingsName());

        m_settings = std::make_unique<Settings>(GetEmptySettings());
    }

    void SettingsProvider::Initialize()
    {
        if (!m_settings)
        {
            m_settings = std::make_unique<Settings>(GetEmptySettings());

            // load from DB or use default
            ByteBuffer settings;
            m_walletDB->getBlob(GetSettingsName(), settings);

            if (!settings.empty())
            {
                Deserializer d;
                d.reset(settings.data(), settings.size());
                d& *m_settings;

                assert(m_settings->GetFeeRate() > 0);
                assert(m_settings->GetMinFeeRate() > 0);
                assert(m_settings->GetMinFeeRate() <= m_settings->GetFeeRate());
            }
        }
    }

    const char* SettingsProvider::GetSettingsName() const
    {
        return "BTCSettings";
    }

    Settings SettingsProvider::GetEmptySettings()
    {
        return Settings{};
    }
} // namespace beam::bitcoin