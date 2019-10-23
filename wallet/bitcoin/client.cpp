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

#include "client.h"

#include "wallet/bitcoin/bitcoin_core_017.h"
#include "utility/logger.h"
#include "utility/bridge.h"

namespace beam::bitcoin
{
    struct BitcoinClientBridge : public Bridge<IClientAsync>
    {
        BRIDGE_INIT(BitcoinClientBridge);

        void GetStatus()
        {
            call_async(&IClientAsync::GetStatus);
        }

        void GetBalance()
        {
            call_async(&IClientAsync::GetBalance);
        }
    };
    
    Client::Client(IBridgeHolder::Ptr bridgeHolder, std::unique_ptr<SettingsProvider> settingsProvider, io::Reactor& reactor)
        : m_status(settingsProvider->GetSettings().IsActivated() ? Status::Connecting : Status::Uninitialized)
        , m_reactor(reactor)
        , m_async{ std::make_shared<BitcoinClientBridge>(*(static_cast<IClientAsync*>(this)), reactor) }
        , m_settingsProvider{ std::move(settingsProvider) }
        , m_bridgeHolder(bridgeHolder)
    {
    }

    IClientAsync::Ptr Client::GetAsync()
    {
        return m_async;
    }

    BitcoinCoreSettings Client::GetBitcoinCoreSettings() const
    {
        Lock lock(m_mutex);
        return m_settingsProvider->GetBitcoinCoreSettings();
    }

    ElectrumSettings Client::GetElectrumSettings() const
    {
        Lock lock(m_mutex);
        return m_settingsProvider->GetElectrumSettings();
    }

    Settings Client::GetSettings() const
    {
        Lock lock(m_mutex);
        return m_settingsProvider->GetSettings();
    }

    void Client::SetSettings(const Settings& settings)
    {
        {
            Lock lock(m_mutex);
            m_settingsProvider->SetSettings(settings);
            m_bridgeHolder->Reset();

            if (m_settingsProvider->GetSettings().IsActivated())
            {
                SetStatus(Status::Connecting);
            }
            else
            {
                SetStatus(Status::Uninitialized);
            }
        }

        OnChangedSettings();
    }

    void Client::GetStatus()
    {
        Status status = Status::Unknown;
        {
            Lock lock(m_mutex);
            status = m_status;
        }
        OnStatus(status);
    }

    void Client::GetBalance()
    {
        auto bridge = GetBridge();

        if (!bridge)
        {
            return;
        }

        bridge->getDetailedBalance([this, weak = this->weak_from_this()] (const IBridge::Error& error, Amount confirmed, Amount unconfirmed, Amount immature)
        {
            if (weak.expired())
            {
                return;
            }

            {
                Lock lock(m_mutex);
                // TODO: check error and update status
                SetStatus((error.m_type != IBridge::None) ? Status::Failed : Status::Connected);
            }

            Balance balance;
            balance.m_available = confirmed;
            balance.m_unconfirmed = unconfirmed;
            balance.m_immature = immature;

            OnBalance(balance);
        });
    }

    void Client::ResetSettings()
    {
        Lock lock(m_mutex);
        m_settingsProvider->ResetSettings();
        m_bridgeHolder->Reset();

        SetStatus(Status::Uninitialized);
    }

    void Client::SetStatus(const Status& status)
    {
        if (m_status != status)
        {
            m_status = status;
            OnStatus(m_status);
        }
    }

    beam::bitcoin::IBridge::Ptr Client::GetBridge()
    {
        return m_bridgeHolder->Get(m_reactor, *this);
    }

    bool Client::CanModify() const
    {
        return m_refCount == 0;
    }

    void Client::AddRef()
    {
        ++m_refCount;
        OnCanModifySettingsChanged(CanModify());
    }

    void Client::ReleaseRef()
    {
        if (m_refCount)
        {
            --m_refCount;
            OnCanModifySettingsChanged(CanModify());
        }
    }

} // namespace beam::bitcoin