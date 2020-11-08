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
#include "bridge.h"
#include "settings_provider.h"
#include "bridge_holder.h"

namespace beam::ethereum
{
class IClientAsync
{
public:
    using Ptr = std::shared_ptr<IClientAsync>;

    virtual void GetStatus() = 0;
    virtual void GetBalance() = 0;
    virtual void EstimateGasPrice() = 0;
    virtual void ChangeSettings(const Settings& settings) = 0;
};

class Client
    : private IClientAsync
    , public ISettingsProvider
    , public std::enable_shared_from_this<ISettingsProvider>
{
public:

    enum class Status
    {
        Uninitialized,
        Initialized,
        Connecting,
        Connected,
        Failed,
        Unknown
    };

    Client(IBridgeHolder::Ptr bridgeHolder, std::unique_ptr<SettingsProvider> settingsProvider, io::Reactor& reactor);

    IClientAsync::Ptr GetAsync();

    Settings GetSettings() const override;
    void SetSettings(const Settings& settings) override;

protected:
    virtual void OnStatus(Status status) = 0;
    // balance in gwei
    virtual void OnBalance(Amount balance) = 0;
    virtual void OnEstimatedGasPrice(Amount gasPrice) = 0;
    virtual void OnCanModifySettingsChanged(bool canModify) = 0;
    virtual void OnChangedSettings() = 0;
    virtual void OnConnectionError(IBridge::ErrorType error) = 0;

    bool CanModify() const override;
    void AddRef() override;
    void ReleaseRef() override;

private:
    // IClientAsync
    void GetStatus() override;
    void GetBalance() override;
    void EstimateGasPrice() override;
    void ChangeSettings(const Settings& settings) override;

    void SetStatus(const Status& status);
    IBridge::Ptr GetBridge();

    void SetConnectionError(const IBridge::ErrorType& error);

private:
    Status m_status;
    io::Reactor& m_reactor;
    IClientAsync::Ptr m_async;
    std::unique_ptr<SettingsProvider> m_settingsProvider;
    IBridgeHolder::Ptr m_bridgeHolder;

    mutable std::mutex m_mutex;
    using Lock = std::unique_lock<std::mutex>;
    size_t m_refCount = 0;
    IBridge::ErrorType m_connectionError = IBridge::ErrorType::None;
};
} // namespace beam::ethereum