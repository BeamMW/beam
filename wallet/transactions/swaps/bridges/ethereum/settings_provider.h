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

#include "core/serialization_adapters.h"
#include "utility/common.h"
#include "wallet/core/common.h"
#include "wallet/core/wallet_db.h"
#include "settings.h"

namespace beam::ethereum
{
class ISettingsProvider
{
public:
    using Ptr = std::shared_ptr<ISettingsProvider>;

    virtual ~ISettingsProvider() = default;
    virtual Settings GetSettings() const = 0;
    virtual void SetSettings(const Settings& settings) = 0;
    virtual bool CanModify() const = 0;
    virtual void AddRef() = 0;
    virtual void ReleaseRef() = 0;
};

class SettingsProvider
    : public ISettingsProvider
{
public:
    SettingsProvider(wallet::IWalletDB::Ptr walletDB);

    Settings GetSettings() const override;
    void SetSettings(const Settings& settings) override;

    void Initialize();

protected:

    bool CanModify() const override;
    void AddRef() override;
    void ReleaseRef() override;

    virtual std::string GetSettingsName() const;
    virtual Settings GetEmptySettings();

    std::string GetAddressName() const;
    std::string GetSecretWordsName() const;
    std::string GetAccountIndexName() const;
    std::string GetShouldConnectName() const;
    std::string GetContractAddressName() const;
    std::string GetHashlockContractAddressName() const;
        
    template<typename T>
    void ReadFromDB(const std::string& name, T& value)
    {
        ByteBuffer settings;
        m_walletDB->getBlob(name.c_str(), settings);

        if (!settings.empty())
        {
            Deserializer d;
            d.reset(settings.data(), settings.size());
            d& value;
        }
    }

    template<typename T>
    void WriteToDb(const std::string& name, const T& value)
    {
        auto buffer = wallet::toByteBuffer(value);
        m_walletDB->setVarRaw(name.c_str(), buffer.data(), buffer.size());
    }

private:
    wallet::IWalletDB::Ptr m_walletDB;
    std::unique_ptr<Settings> m_settings;
    size_t m_refCount = 0;
};
} // namespace beam::ethereum