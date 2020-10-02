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
#include "http/http_client.h"
#include "settings_provider.h"

namespace beam::ethereum
{
class EthereumBridge : public IBridge
{
public:
    EthereumBridge() = delete;
    EthereumBridge(io::Reactor& reactor, ISettingsProvider& settingsProvider);

    void getBalance(std::function<void(ECC::uintBig)> callback) override;
    void getBlockNumber(std::function<void(Amount)> callback) override;
    void getTransactionCount(std::function<void(Amount)> callback) override;
    void sendRawTransaction(const std::string& rawTx, std::function<void(std::string)> callback) override;
    void getTransactionReceipt(const std::string& txHash, std::function<void()> callback) override;
    void call(const libbitcoin::short_hash& to, const std::string& data, std::function<void()> callback) override;
    libbitcoin::short_hash generateEthAddress() const override;

protected:
    void sendRequest(const std::string& method, const std::string& params, std::function<void(const nlohmann::json&)> callback);
    libbitcoin::ec_secret generatePrivateKey() const;

private:
    HttpClient m_httpClient;
    ISettingsProvider& m_settingsProvider;
};
} // namespace beam::ethereum