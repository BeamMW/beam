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

#include "nlohmann/json.hpp"

namespace beam::io
{
    class TcpStream;
}

namespace libbitcoin::wallet
{
    class ec_private;
    class hd_private;
}

namespace beam::bitcoin
{
    // TODO roman.strilets maybe should to use std::enable_shared_from_this
    class Electrum : public IBridge
    {
    private:
        struct TCPConnect
        {
            std::string m_request;
            std::function<bool(const Error&, const nlohmann::json&, uint64_t)> m_callback;
            std::unique_ptr<beam::io::TcpStream> m_stream;
        };

        struct Utxo
        {
            size_t m_index;
            nlohmann::json m_details;
        };

    public:
        Electrum() = delete;
        Electrum(beam::io::Reactor& reactor, IElectrumSettingsProvider::Ptr settingsProvider);

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
        void getTxOut(const std::string& txid, int outputIndex, std::function<void(const Error&, const std::string&, double, uint32_t)> callback) override;
        void getBlockCount(std::function<void(const Error&, uint64_t)> callback) override;
        void getBalance(uint32_t confirmations, std::function<void(const Error&, double)> callback) override;

        void getDetailedBalance(std::function<void(const Error&, double, double, double)> callback) override;

    protected:
        void listUnspent(std::function<void(const Error&, const std::vector<Utxo>&)> callback);

        void sendRequest(const std::string& method, const std::string& params, std::function<bool(const Error&, const nlohmann::json&, uint64_t)> callback);

        // return the indexth address for private key
        std::string getAddress(uint32_t index, const libbitcoin::wallet::hd_private& privateKey) const;

        // return the list of all private keys (receiving and changing)
        std::vector<libbitcoin::wallet::ec_private> generatePrivateKeyList() const;

        // the first key is receiving master private key
        // the second key is changing master private key
        std::pair<libbitcoin::wallet::hd_private, libbitcoin::wallet::hd_private> generateMasterPrivateKeys() const;

    private:
        beam::io::Reactor& m_reactor;
        std::map<uint64_t, TCPConnect> m_connections;
        uint64_t m_tagCounter = 0;
        uint32_t m_currentReceivingAddress = 0;
        IElectrumSettingsProvider::Ptr m_settingsProvider;
    };
} // namespace beam::bitcoin