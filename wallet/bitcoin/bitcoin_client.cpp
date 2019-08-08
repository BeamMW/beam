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

#include "bitcoin_client.h"

#include "wallet/bitcoin/bitcoind017.h"
#include "utility/logger.h"
#include "utility/bridge.h"

namespace
{
    const char* kBitcoinSettingsName = "BTCSettings";
}

namespace beam
{
    struct BitcoinClientBridge : public Bridge<IBitcoinClientAsync>
    {
        BRIDGE_INIT(BitcoinClientBridge);

        void GetStatus()
        {
            call_async(&IBitcoinClientAsync::GetStatus);
        }

        void GetBalance()
        {
            call_async(&IBitcoinClientAsync::GetBalance);
        }

        void ResetSettings()
        {
            call_async(&IBitcoinClientAsync::ResetSettings);
        }
    };
    
    BitcoinClient::BitcoinClient(wallet::IWalletDB::Ptr walletDB, io::Reactor& reactor)
        : m_status(Status::Uninitialized)
        , m_walletDB(walletDB)
        , m_reactor(reactor)
        , m_async{ std::make_shared<BitcoinClientBridge>(*(static_cast<IBitcoinClientAsync*>(this)), reactor) }
    {
        LoadSettings();
    }

    IBitcoinClientAsync::Ptr BitcoinClient::GetAsync()
    {
        return m_async;
    }

    BitcoindSettings BitcoinClient::GetBitcoindSettings() const
    {
        Lock lock(m_mutex);
        assert(m_settings);
        return m_settings->GetConnectionOptions();
    }

    BitcoinSettings BitcoinClient::GetSettings() const
    {
        Lock lock(m_mutex);
        assert(m_settings);
        return *m_settings;
    }

    void BitcoinClient::SetSettings(const BitcoinSettings& settings)
    {
        Lock lock(m_mutex);

        // store to DB
        auto buffer = wallet::toByteBuffer(settings);
        m_walletDB->setVarRaw(kBitcoinSettingsName, buffer.data(), static_cast<int>(buffer.size()));

        // update m_settings
        m_settings = std::make_unique<BitcoinSettings>(settings);
    }

    void BitcoinClient::GetStatus()
    {
        OnStatus(m_status);
    }

    void BitcoinClient::GetBalance()
    {
        if (!m_bridge)
        {
            m_bridge = std::make_shared<Bitcoind017>(m_reactor, shared_from_this());
        }

        m_bridge->getDetailedBalance([this] (const IBitcoinBridge::Error& error, double confirmed, double unconfirmed, double immature)
        {
            // TODO: check error and update status
            SetStatus((error.m_type != IBitcoinBridge::None) ? Status::Failed : Status::Connected);

            Balance balance;
            balance.m_available = confirmed;
            balance.m_unconfirmed = unconfirmed;
            balance.m_immature = immature;

            OnBalance(balance);
        });
    }

    void BitcoinClient::ResetSettings()
    {
        {
            Lock lock(m_mutex);

            // remove from DB
            m_walletDB->removeVarRaw(kBitcoinSettingsName);

            m_settings = std::make_unique<BitcoinSettings>();
        }

        SetStatus(Status::Uninitialized);
    }

    void BitcoinClient::LoadSettings()
    {
        if (!m_settings)
        {
            m_settings = std::make_unique<BitcoinSettings>();

            // load from DB or use default
            ByteBuffer settings;
            m_walletDB->getBlob(kBitcoinSettingsName, settings);

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

    void BitcoinClient::SetStatus(const Status& status)
    {
        m_status = status;
        OnStatus(m_status);
    }
}