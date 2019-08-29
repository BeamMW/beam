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

#include "bridge.h"
#include "settings_provider.h"
#include "wallet/common.h"
#include "wallet/wallet_db.h"

namespace beam::bitcoin
{
    class IClientAsync
    {
    public:
        using Ptr = std::shared_ptr<IClientAsync>;

        virtual void GetStatus() = 0;
        virtual void GetBalance() = 0;
        virtual void ResetSettings() = 0;
    };

    class Client 
        : private IClientAsync
        , public ISettingsProvider
        , public std::enable_shared_from_this<ISettingsProvider>
    {
    public:

        using CreateBridge = std::function<IBridge::Ptr(io::Reactor& reactor, IBitcoinCoreSettingsProvider::Ptr settingsProvider)>;

        struct Balance
        {
            double m_available = 0;
            double m_unconfirmed = 0;
            double m_immature = 0;
        };

        enum class Status
        {
            Uninitialized,
            Connected,
            Failed,
            Unknown
        };

        Client(CreateBridge bridgeCreator, std::unique_ptr<SettingsProvider> settingsProvider, io::Reactor& reactor);

        IClientAsync::Ptr GetAsync();

        BitcoinCoreSettings GetBitcoinCoreSettings() const override;
        ElectrumSettings GetElectrumSettings() const override;
        Settings GetSettings() const override;
        void SetSettings(const Settings& settings) override;

    protected:
        virtual void OnStatus(Status status) = 0;
        virtual void OnBalance(const Balance& balance) = 0;

    private:
        // IClientAsync
        void GetStatus() override;
        void GetBalance() override;

        // ISettingsProvider
        void ResetSettings() override;

        void SetStatus(const Status& status);
        IBridge::Ptr GetBridge();

    private:
        Status m_status;
        io::Reactor& m_reactor;
        IClientAsync::Ptr m_async;
        IBridge::Ptr m_bridge;
        std::unique_ptr<SettingsProvider> m_settingsProvider;
        CreateBridge m_bridgeCreator;

        mutable std::mutex m_mutex;
        using Lock = std::unique_lock<std::mutex>;
    };
} // namespace beam::bitcoin