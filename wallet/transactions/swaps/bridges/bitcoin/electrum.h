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

#include <memory>
#include <chrono>

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
    class Electrum : public IBridge, public std::enable_shared_from_this<Electrum>
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

        struct LockUtxo
        {
            std::string m_txHash;
            uint32_t m_pos;
            std::chrono::system_clock::time_point m_time;
        };

    public:
        Electrum() = delete;
        Electrum(beam::io::Reactor& reactor, ISettingsProvider& settingsProvider);
        ~Electrum() override;

        void fundRawTransaction(const std::string& rawTx, Amount feeRate, std::function<void(const Error&, const std::string&, int)> callback) override;
        void signRawTransaction(const std::string& rawTx, std::function<void(const Error&, const std::string&, bool)> callback) override;
        void sendRawTransaction(const std::string& rawTx, std::function<void(const Error&, const std::string&)> callback) override;
        void getRawChangeAddress(std::function<void(const Error&, const std::string&)> callback) override;
        void createRawTransaction(
            const std::string& withdrawAddress,
            const std::string& contractTxId,
            Amount amount,
            int outputIndex,
            Timestamp locktime,
            std::function<void(const Error&, const std::string&)> callback) override;
        void getTxOut(const std::string& txid, int outputIndex, std::function<void(const Error&, const std::string&, Amount, uint32_t)> callback) override;
        void getBlockCount(std::function<void(const Error&, uint64_t)> callback) override;
        void getBalance(uint32_t confirmations, std::function<void(const Error&, Amount)> callback) override;

        void getDetailedBalance(std::function<void(const Error&, Amount, Amount, Amount)> callback) override;

        void getGenesisBlockHash(std::function<void(const Error&, const std::string&)> callback) override;

    protected:
        void listUnspent(std::function<void(const Error&, const std::vector<Utxo>&)> callback);

        void sendRequest(const std::string& method, const std::string& params, std::function<bool(const Error&, const nlohmann::json&, uint64_t)> callback);

        // return the list of all private keys (receiving and changing)
        std::vector<libbitcoin::wallet::ec_private> generatePrivateKeyList() const;

        void lockUtxo(std::string hash, uint32_t pos);
        bool isLockedUtxo(std::string hash, uint32_t pos);
        void reviewLockedUtxo();

        void tryToChangeAddress();

        bool isNodeAddressCheckedAndVerified(const std::string& address) const;

    private:
        beam::io::Reactor& m_reactor;
        std::map<uint64_t, TCPConnect> m_connections;
        uint64_t m_idCounter = 0;
        ISettingsProvider& m_settingsProvider;
        std::size_t m_currentAddressIndex = 0;

        std::vector<LockUtxo> m_lockedUtxo;
        std::vector<Utxo> m_cache;
        std::chrono::system_clock::time_point m_lastCache;
        io::AsyncEvent::Ptr m_asyncEvent;
        std::map<std::string, bool> m_verifiedAddresses;
    };
} // namespace beam::bitcoin