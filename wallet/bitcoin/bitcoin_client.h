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

#include "bitcoin_bridge.h"
#include "bitcoin_settings.h"
#include "wallet/common.h"
#include "wallet/wallet_db.h"

namespace beam
{
    class IBitcoinClientAsync
    {
    public:
        using Ptr = std::shared_ptr<IBitcoinClientAsync>;

        virtual void GetStatus() = 0;
        virtual void GetBalance() = 0;
    };

    class BitcoinClient 
        : private IBitcoinClientAsync
        , public IBitcoinSettingsProvider
        , public std::enable_shared_from_this<IBitcoinSettingsProvider>
    {
    public:

        struct Balance
        {
            double m_available = 0;
            double m_unconfirmed = 0;
            double m_immature = 0;
        };

        enum class Status
        {
            Disconnected,
            Connected,
            Failed,
            Unknown
        };

        BitcoinClient(wallet::IWalletDB::Ptr walletDB, io::Reactor& reactor);

        IBitcoinClientAsync::Ptr GetAsync();

        BitcoindSettings GetBitcoindSettings() const override;
        BitcoinSettings GetSettings() const override;
        void SetSettings(const BitcoinSettings& settings) override;

    protected:
        virtual void OnStatus(Status status) = 0;
        virtual void OnBalance(const Balance& balance) = 0;

    private:
        // IBitcoinClientAsync
        void GetStatus() override;
        void GetBalance() override;
    
        void LoadSettings();

    private:
        Status m_status;
        wallet::IWalletDB::Ptr m_walletDB;
        io::Reactor& m_reactor;
        IBitcoinClientAsync::Ptr m_async;
        std::shared_ptr<BitcoinSettings> m_settings;
        IBitcoinBridge::Ptr m_bridge;

        mutable std::mutex m_mutex;
        using Lock = std::unique_lock<std::mutex>;
    };
}