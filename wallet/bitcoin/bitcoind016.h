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
#include "http/http_client.h"
#include "options.h"

namespace beam
{
    class Bitcoind016: public IBitcoinBridge
    {
    public:
        Bitcoind016() = delete;
        Bitcoind016(io::Reactor& reactor, const BitcoinOptions& options);

        void dumpPrivKey(const std::string& btcAddress, std::function<void(const Error&, const std::string&)> callback) override;
        void fundRawTransaction(const std::string& rawTx, Amount feeRate, std::function<void(const Error&, const std::string&, int)> callback) override;
        void signRawTransaction(const std::string& rawTx, std::function<void(const Error&, const std::string&, bool)> callback) override;
        void sendRawTransaction(const std::string& rawTx, std::function<void(const Error&, const std::string&)> callback) override;
        void getRawChangeAddress(std::function<void(const Error&, const std::string&)> callback) override;
        void createRawTransaction(
            const std::string& withdrawAddress,
            const std::string& contractTxId,
            uint64_t amount,
            int outputIndex,
            Timestamp locktime,
            std::function<void(const Error&, const std::string&)> callback) override;
        void getTxOut(const std::string& txid, int outputIndex, std::function<void(const Error&, const std::string&, double, uint16_t)> callback) override;
        void getBlockCount(std::function<void(const Error&, uint64_t)> callback) override;
        void getBalance(uint32_t confirmations, std::function<void(const Error&, double)> callback) override;

        uint8_t getAddressVersion() override;
        Amount getFeeRate() const override;
        uint16_t getTxMinConfirmations() const override;
        uint32_t getLockTimeInBlocks() const override;
        std::string getCoinName() const override;

    protected:
        void sendRequest(const std::string& method, const std::string& params, std::function<void(const Error&, const nlohmann::json&)> callback);
        bool isMainnet() const;

    private:
        HttpClient m_httpClient;
        BitcoinOptions m_options;
        std::string m_authorization;
    };
}