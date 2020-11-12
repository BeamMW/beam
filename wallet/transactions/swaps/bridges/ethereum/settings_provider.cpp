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

#include "settings_provider.h"

namespace beam::ethereum
{
SettingsProvider::SettingsProvider(wallet::IWalletDB::Ptr walletDB)
    : m_walletDB(walletDB)
{
}

Settings SettingsProvider::GetSettings() const
{
    assert(m_settings);
    return *m_settings;
}

void SettingsProvider::SetSettings(const Settings& settings)
{
    // store to DB
    WriteToDb(GetAddressName(), settings.m_address);
    WriteToDb(GetSecretWordsName(), settings.m_secretWords);
    WriteToDb(GetAccountIndexName(), settings.m_accountIndex);
    WriteToDb(GetShouldConnectName(), settings.m_shouldConnect);
    WriteToDb(GetContractAddressName(), settings.m_swapContractAddress);
    WriteToDb(GetDaiContractAddressName(), settings.m_daiContractAddress);
    WriteToDb(GetUsdtContractAddressName(), settings.m_usdtContractAddress);
    WriteToDb(GetWbtcContractAddressName(), settings.m_wbtcContractAddress);
    WriteToDb(GetHashlockContractAddressName(), settings.m_swapHashlockContractAddress);
    
    // update m_settings
    m_settings = std::make_unique<Settings>(settings);
}

void SettingsProvider::Initialize()
{
    if (!m_settings)
    {
        m_settings = std::make_unique<Settings>(GetEmptySettings());
        ReadFromDB(GetAddressName(), m_settings->m_address);
        ReadFromDB(GetSecretWordsName(), m_settings->m_secretWords);
        ReadFromDB(GetAccountIndexName(), m_settings->m_accountIndex);
        ReadFromDB(GetShouldConnectName(), m_settings->m_shouldConnect);
        ReadFromDB(GetContractAddressName(), m_settings->m_swapContractAddress);
        ReadFromDB(GetWbtcContractAddressName(), m_settings->m_wbtcContractAddress);
        ReadFromDB(GetDaiContractAddressName(), m_settings->m_daiContractAddress);
        ReadFromDB(GetUsdtContractAddressName(), m_settings->m_usdtContractAddress);
        ReadFromDB(GetHashlockContractAddressName(), m_settings->m_swapHashlockContractAddress);
    }
}

bool SettingsProvider::CanModify() const
{
    return m_refCount == 0;
}

void SettingsProvider::AddRef()
{
    ++m_refCount;
}

void SettingsProvider::ReleaseRef()
{
    if (m_refCount)
    {
        --m_refCount;
    }
}

std::string SettingsProvider::GetSettingsName() const
{
    return "ETHSettings";
}

Settings SettingsProvider::GetEmptySettings()
{
    return Settings{};
}

std::string SettingsProvider::GetAddressName() const
{
    return GetSettingsName() + "_Address";
}

std::string SettingsProvider::GetSecretWordsName() const
{
    return GetSettingsName() + "_SecretWords";
}

std::string SettingsProvider::GetAccountIndexName() const
{
    return GetSettingsName() + "_AccountIndex";
}

std::string SettingsProvider::GetShouldConnectName() const
{
    return GetSettingsName() + "_ShouldConnect";
}

std::string SettingsProvider::GetContractAddressName() const
{
    return GetSettingsName() + "_ContractAddress";
}

std::string SettingsProvider::GetDaiContractAddressName() const
{
    return GetSettingsName() + "_DaiContractAddress";
}

std::string SettingsProvider::GetWbtcContractAddressName() const
{
    return GetSettingsName() + "_WbtcContractAddress";
}

std::string SettingsProvider::GetUsdtContractAddressName() const
{
    return GetSettingsName() + "_UsdtContractAddress";
}

std::string SettingsProvider::GetHashlockContractAddressName() const
{
    return GetSettingsName() + "_HahslockContractAddress";
}
} // namespace beam::ethereum