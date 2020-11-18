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

#include "client.h"

#include "utility/logger.h"
#include "utility/bridge.h"

#include "ethereum_bridge.h"
#include "common.h"

namespace beam::ethereum
{
namespace
{
bool IsChangedConnectionSettings(const Settings& currentSettings, const Settings& newSettings)
{
    bool isConnectionTypeChanged = currentSettings.m_shouldConnect != newSettings.m_shouldConnect;

    if (isConnectionTypeChanged)
    {
        return true;
    }

    if (currentSettings.m_shouldConnect)
    {
        return currentSettings != newSettings;
    }

    return false;
}
}

struct EthereumClientBridge : public Bridge<IClientAsync>
{
    BRIDGE_INIT(EthereumClientBridge);

    void GetStatus()
    {
        call_async(&IClientAsync::GetStatus);
    }

    void GetBalance(wallet::AtomicSwapCoin swapCoin)
    {
        call_async(&IClientAsync::GetBalance, swapCoin);
    }

    void EstimateGasPrice()
    {
        call_async(&IClientAsync::EstimateGasPrice);
    }

    void ChangeSettings(const Settings& settings)
    {
        call_async(&IClientAsync::ChangeSettings, settings);
    }
};

Client::Client(IBridgeHolder::Ptr bridgeHolder, std::unique_ptr<SettingsProvider> settingsProvider, io::Reactor& reactor)
    : m_status(settingsProvider->GetSettings().IsActivated() ? Status::Connecting :
        settingsProvider->GetSettings().IsInitialized() ? Status::Initialized : Status::Uninitialized)
    , m_reactor(reactor)
    , m_async{ std::make_shared<EthereumClientBridge>(*(static_cast<IClientAsync*>(this)), reactor) }
    , m_settingsProvider{ std::move(settingsProvider) }
    , m_bridgeHolder(bridgeHolder)
{
}

IClientAsync::Ptr Client::GetAsync()
{
    return m_async;
}

Settings Client::GetSettings() const
{
    Lock lock(m_mutex);
    return m_settingsProvider->GetSettings();
}

void Client::SetSettings(const Settings& settings)
{
    GetAsync()->ChangeSettings(settings);
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

void Client::GetBalance(wallet::AtomicSwapCoin swapCoin)
{
    auto bridge = GetBridge();

    if (!bridge)
    {
        return;
    }

    if (swapCoin == wallet::AtomicSwapCoin::Ethereum)
    {
        bridge->getBalance([this, weak = this->weak_from_this()](const IBridge::Error& error, const std::string& balance)
        {
            if (weak.expired())
            {
                return;
            }

            // TODO: check error and update status
            SetConnectionError(error.m_type);
            SetStatus((error.m_type != IBridge::None) ? Status::Failed : Status::Connected);

            if (error.m_type == IBridge::None)
            {
                boost::multiprecision::uint256_t tmp(balance);

                tmp /= ethereum::GetCoinUnitsMultiplier(wallet::AtomicSwapCoin::Ethereum);

                OnBalance(wallet::AtomicSwapCoin::Ethereum, tmp.convert_to<Amount>());
            }
        });
    }
    else
    {
        const auto tokenContractAddressStr = m_settingsProvider->GetSettings().GetTokenContractAddress(swapCoin);
        if (tokenContractAddressStr.empty())
        {
            return;
        }

        bridge->getTokenBalance(tokenContractAddressStr, [this, weak = this->weak_from_this(), swapCoin](const IBridge::Error& error, const std::string& balance)
        {
            if (weak.expired())
            {
                return;
            }

            // TODO: check error and update status
            SetConnectionError(error.m_type);
            SetStatus((error.m_type != IBridge::None) ? Status::Failed : Status::Connected);

            if (error.m_type == IBridge::None)
            {
                boost::multiprecision::uint256_t tmp(balance);
                tmp /= ethereum::GetCoinUnitsMultiplier(swapCoin);

                OnBalance(swapCoin, tmp.convert_to<Amount>());
            }
        });
    }
}

void Client::EstimateGasPrice()
{
    auto bridge = GetBridge();

    if (!bridge)
    {
        return;
    }

    bridge->getGasPrice([this, weak = this->weak_from_this()](const IBridge::Error& error, Amount gasPrice)
    {
        if (weak.expired())
        {
            return;
        }

        // TODO: check error and update status
        SetConnectionError(error.m_type);
        SetStatus((error.m_type != IBridge::None) ? Status::Failed : Status::Connected);

        if (IBridge::None == error.m_type)
        {
            // convert from wei to gwei
            Amount result = gasPrice / ethereum::GetCoinUnitsMultiplier(wallet::AtomicSwapCoin::Ethereum);

            OnEstimatedGasPrice(result);
        }
    });
}

void Client::ChangeSettings(const Settings& settings)
{
    {
        Lock lock(m_mutex);
        auto currentSettings = m_settingsProvider->GetSettings();
        bool shouldReconnect = IsChangedConnectionSettings(currentSettings, settings);

        m_settingsProvider->SetSettings(settings);

        if (shouldReconnect)
        {
            m_bridgeHolder->Reset();

            if (m_settingsProvider->GetSettings().IsActivated())
            {
                SetStatus(Status::Connecting);
            }
            else if (m_settingsProvider->GetSettings().IsInitialized())
            {
                SetStatus(Status::Initialized);
            }
            else
            {
                SetStatus(Status::Uninitialized);
            }
        }
        else if (!m_settingsProvider->GetSettings().IsActivated())
        {
            if (m_settingsProvider->GetSettings().IsInitialized())
            {
                SetStatus(Status::Initialized);
            }
            else
            {
                SetStatus(Status::Uninitialized);
            }
        }
    }

    OnChangedSettings();
}

void Client::SetStatus(const Status& status)
{
    if (m_status != status)
    {
        m_status = status;
        OnStatus(m_status);
    }
}

IBridge::Ptr Client::GetBridge()
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

void Client::SetConnectionError(const IBridge::ErrorType& error)
{
    if (m_connectionError != error)
    {
        m_connectionError = error;
        OnConnectionError(m_connectionError);
    }
}
} // namespace beam::ethereum