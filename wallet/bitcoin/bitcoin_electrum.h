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

#include "bitcoind016.h"
#include "options.h"

#include "bitcoin/bitcoin.hpp"

#include "nlohmann/json.hpp"

namespace beam
{
    namespace io
    {
        class TcpStream;
    }

    class BitcoinElectrum : public Bitcoind016
    {
    private:
        struct TCPConnect
        {
            std::string m_request;
            std::function<bool(const Error&, const nlohmann::json&, uint64_t)> m_callback;
            std::unique_ptr<io::TcpStream> m_stream;
        };

        // TODO roman.strilets it is temporary
    public:

        struct BtcCoin
        {
            libbitcoin::wallet::ec_private m_privateKey;
            nlohmann::json m_details;
        };

    public:
        BitcoinElectrum() = delete;
        BitcoinElectrum(io::Reactor& reactor, const BitcoinOptions& options);

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
        // TODO roman.strilec use uint32_t instead uint16_t
        void getTxOut(const std::string& txid, int outputIndex, std::function<void(const Error&, const std::string&, double, uint16_t)> callback) override;
        void getBlockCount(std::function<void(const Error&, uint64_t)> callback) override;
        void getBalance(uint32_t confirmations, std::function<void(const Error&, double)> callback) override;

        void listUnspent(std::function<void(const Error&, const std::vector<BtcCoin>&)> callback);

        /*uint8_t getAddressVersion() override;
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
        std::string m_authorization;*/

    protected:

        virtual uint32_t getReceivingAddressAmount() const;
        virtual uint32_t getChangeAddressAmount() const;

        void sendRequest(const std::string& method, const std::string& params, std::function<bool(const Error&, const nlohmann::json&, uint64_t)> callback);
        std::string getReceivingAddress(uint32_t index) const;
        std::string getChangeAddress(uint32_t index) const;
        std::vector<libbitcoin::wallet::ec_private> generatePrivateKeyList() const;

    private:
        io::Reactor& m_reactor;
        std::map<uint64_t, TCPConnect> m_connections;
        libbitcoin::wallet::hd_private m_receivingPrivateKey;
        libbitcoin::wallet::hd_private m_changePrivateKey;
        uint64_t m_counter = 0;
        uint32_t m_currentReceivingAddress = 0;
    };
}