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
#include "ethereum_base_transaction.h"

#include <memory>

namespace beam::ethereum
{
class EthereumBridge : public IBridge, public std::enable_shared_from_this<EthereumBridge>
{
public:
    EthereumBridge() = delete;
    EthereumBridge(io::Reactor& reactor, ISettingsProvider& settingsProvider);

    void getBalance(std::function<void(const Error&, const std::string&)> callback) override;
    void getTokenBalance(
        const std::string& contractAddr,
        std::function<void(const Error&, const std::string&)> callback) override;
    void getBlockNumber(std::function<void(const Error&, uint64_t)> callback) override;
    void getTransactionCount(std::function<void(const Error&, uint64_t)> callback) override;
    void sendRawTransaction(const std::string& rawTx, std::function<void(const Error&, std::string)> callback) override;
    void send(
        const libbitcoin::short_hash& to,
        const libbitcoin::data_chunk& data,
        const ECC::uintBig& value,
        const ECC::uintBig& gas,
        const ECC::uintBig& gasPrice,
        std::function<void(const Error&, std::string, uint64_t)> callback) override;
    void getTransactionReceipt(const std::string& txHash, std::function<void(const Error&, const nlohmann::json&)> callback) override;
    void getTxBlockNumber(const std::string& txHash, std::function<void(const Error&, uint64_t)> callback) override;
    void getTxByHash(const std::string& txHash, std::function<void(const Error&, const nlohmann::json&)> callback) override;
    void call(const libbitcoin::short_hash& to, const std::string& data, std::function<void(const Error&, const nlohmann::json&)> callback) override;
    libbitcoin::short_hash generateEthAddress() const override;
    void getGasPrice(std::function<void(const Error&, Amount)> callback) override;

protected:
    void sendRequest(
        const std::string& method, 
        const std::string& params, 
        std::function<void(const Error&, const nlohmann::json&)> callback);
    libbitcoin::ec_secret generatePrivateKey() const;

private:

    void processPendingTx();
    void onGotTransactionCount(const Error& error, uint64_t txCount);
    void onSentRawTransaction(const Error& error, const std::string& txHash, uint64_t txNonce);
    void requestTxConfirmation();
    void onGotTxConfirmation(const Error& error, const nlohmann::json& result);

    struct EthPendingTransaction
    {
        EthBaseTransaction m_ethBaseTx;
        std::function<void(const Error&, std::string, uint64_t)> m_confirmedCallback;
        std::string m_txHash;
    };

    HttpClient m_httpClient;
    ISettingsProvider& m_settingsProvider;

    io::Timer::Ptr m_txConfirmationTimer;
    std::queue<std::shared_ptr<EthPendingTransaction>> m_pendingTxs;
};
} // namespace beam::ethereum