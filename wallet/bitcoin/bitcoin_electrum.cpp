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

#include "bitcoin_electrum.h"


#include "nlohmann/json.hpp"
#include "utility/io/reactor.h"
#include "utility/io/timer.h"
#include "utility/io/tcpstream.h"
#include "utility/config.h"
#include "utility/helpers.h"
#include "utility/logger.h"

#include "bitcoin/bitcoin.hpp"

using json = nlohmann::json;
using namespace beam;
using namespace beam::io;
using namespace libbitcoin;
using namespace libbitcoin::wallet;
using namespace libbitcoin::chain;

namespace
{
    constexpr uint32_t kReceivingAddressAmount = 21;
    constexpr uint32_t kChangeAddressAmount = 6;   

    std::string generateScriptHash(const ec_public& publicKey)
    {
        payment_address addr = publicKey.to_payment_address(ec_private::testnet);
        auto script = chain::script(chain::script::to_pay_key_hash_pattern(addr.hash()));
        libbitcoin::data_chunk secretHash = libbitcoin::sha256_hash_chunk(script.to_data(false));
        libbitcoin::data_chunk reverseSecretHash(secretHash.rbegin(), secretHash.rend());
        return libbitcoin::encode_base16(reverseSecretHash);
    }
}

namespace beam
{
    BitcoinElectrum::BitcoinElectrum(io::Reactor& reactor, const BitcoinOptions& options)
        : Bitcoind016(reactor, options)
        , m_reactor(reactor)
    {
        word_list my_word_list{ "child", "happy", "moment", "weird", "ten", "token", "stuff", "surface", "success", "desk", "embark", "observe" };

        auto hd_seed = electrum::decode_mnemonic(my_word_list);
        data_chunk seed_chunk(to_chunk(hd_seed));
        hd_private masterPrivateKey(seed_chunk, hd_public::testnet);

        m_receivingPrivateKey = masterPrivateKey.derive_private(0);
        m_changePrivateKey = masterPrivateKey.derive_private(1);
    }

    void BitcoinElectrum::dumpPrivKey(const std::string& btcAddress, std::function<void(const IBitcoinBridge::Error&, const std::string&)> callback)
    {
        LOG_DEBUG() << "Send dumpPrivKey command";

        Error error{ None, "" };

        for (uint32_t i = 0; i < kReceivingAddressAmount; ++i)
        {
            if (btcAddress == getReceivingAddress(i))
            {
                ec_private privateKey(m_receivingPrivateKey.derive_private(i).secret(), getAddressVersion());
                callback(error, privateKey.encoded());
                return;
            }
        }

        for (uint32_t i = 0; i < kChangeAddressAmount; ++i)
        {
            if (btcAddress == getChangeAddress(i))
            {
                ec_private privateKey(m_changePrivateKey.derive_private(i).secret(), getAddressVersion());
                callback(error, privateKey.encoded());
                return;
            }
        }

        // TODO roman.strilec proccess error
    }

    void BitcoinElectrum::fundRawTransaction(const std::string& rawTx, Amount feeRate, std::function<void(const IBitcoinBridge::Error&, const std::string&, int)> callback)
    {
        LOG_DEBUG() << "Send fundRawTransaction command";
    }

    void BitcoinElectrum::signRawTransaction(const std::string& rawTx, std::function<void(const IBitcoinBridge::Error&, const std::string&, bool)> callback)
    {
        LOG_DEBUG() << "Send signRawTransaction command";

        data_chunk txData;
        decode_base16(txData, rawTx);
        transaction tx = transaction::factory_from_data(txData);

        for (size_t ind = 0; ind < tx.inputs().size(); ++ind)
        {

        }
    }

    void BitcoinElectrum::sendRawTransaction(const std::string& rawTx, std::function<void(const IBitcoinBridge::Error&, const std::string&)> callback)
    {
        LOG_DEBUG() << "Send sendRawTransaction command";    

        sendRequest("blockchain.transaction.broadcast", rawTx, [callback](IBitcoinBridge::Error error, const json& result, uint64_t)
        {
            std::string txID;

            if (error.m_type == IBitcoinBridge::None)
            {
                try
                {
                    txID = result.get<std::string>();
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBitcoinBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }

            callback(error, txID);
            return false;
        });
    }

    void BitcoinElectrum::getRawChangeAddress(std::function<void(const IBitcoinBridge::Error&, const std::string&)> callback)
    {
        LOG_DEBUG() << "Send getRawChangeAddress command";

        Error error{ None, "" };
        callback(error, getReceivingAddress(m_currentReceivingAddress++));

        if (m_currentReceivingAddress >= kReceivingAddressAmount)
        {
            m_currentReceivingAddress = 0;
        }
    }

    void BitcoinElectrum::createRawTransaction(
        const std::string& withdrawAddress,
        const std::string& contractTxId,
        uint64_t amount,
        int outputIndex,
        Timestamp locktime,
        std::function<void(const IBitcoinBridge::Error&, const std::string&)> callback)
    {
        LOG_DEBUG() << "Send createRawTransaction command";
        hash_digest utxoHash;
        decode_hash(utxoHash, contractTxId);

        output_point utxo(utxoHash, outputIndex);
        input inp;

        inp.set_previous_output(utxo);

        payment_address destinationAddress(withdrawAddress);
        script outputScript = script().to_pay_key_hash_pattern(destinationAddress.hash());
        output out(amount, outputScript);

        transaction tx;

        tx.inputs().push_back(inp);
        tx.outputs().push_back(out);

        tx.set_locktime(static_cast<uint32_t>(locktime));

        tx.set_version(2);

        Error error{ None, "" };
        callback(error, encode_base16(tx.to_data()));
    }

    void BitcoinElectrum::getTxOut(const std::string& txid, int outputIndex, std::function<void(const IBitcoinBridge::Error&, const std::string&, double, uint16_t)> callback)
    {
        LOG_DEBUG() << "Send getTxOut command";
        sendRequest("blockchain.transaction.get", "\"" + txid + "\", true", [callback, outputIndex](IBitcoinBridge::Error error, const json& result, uint64_t)
        {
            double value = 0;
            uint32_t confirmations = 0;
            std::string scriptHex;

            if (error.m_type == IBitcoinBridge::None)
            {
                try
                {
                    confirmations = result["confirmations"].get<uint16_t>();

                    for (const auto& vout : result["vout"])
                    {
                        if (vout["n"].get<int>() == outputIndex)
                        {
                            scriptHex = vout["scriptPubKey"]["hex"].get<std::string>();
                            value = vout["value"].get<double>();
                            break;
                        }
                    }

                    // TODO roman.strilec process unknown vout
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBitcoinBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }

            callback(error, scriptHex, value, confirmations);
            return false;
        });
    }

    void BitcoinElectrum::getBlockCount(std::function<void(const IBitcoinBridge::Error&, uint64_t)> callback)
    {
        LOG_DEBUG() << "Send getBlockCount command";

        sendRequest("blockchain.scripthash.get_balance", "", [callback](IBitcoinBridge::Error error, const json& result, uint64_t)
        {
            uint64_t blockCount = 0;

            if (error.m_type == IBitcoinBridge::None)
            {
                try
                {
                    blockCount = result["height"].get<uint64_t>();
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBitcoinBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }
            callback(error, blockCount);
            return false;
        });
    }

    void BitcoinElectrum::getBalance(uint32_t confirmations, std::function<void(const Error&, double)> callback)
    {
        LOG_DEBUG() << "Send getBalance command";

        struct
        {
            size_t m_index = 0;
            double m_confirmed = 0;
            double m_unconfirmed = 0;
        } tmp;

        auto privateKeys = generatePrivateKeyList();

        sendRequest("blockchain.scripthash.get_balance", "\"" + generateScriptHash(privateKeys[0].to_public()) + "\"",
            [this, callback, tmp, privateKeys](IBitcoinBridge::Error error, const json& result, uint64_t tag) mutable
        {
            if (error.m_type == IBitcoinBridge::None)
            {
                TCPConnect& connection = m_connections[tag];

                try
                {
                    tmp.m_confirmed += result["confirmed"].get<double>();
                    tmp.m_unconfirmed += result["unconfirmed"].get<double>();
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBitcoinBridge::InvalidResultFormat;
                    error.m_message = ex.what();

                    callback(error, tmp.m_confirmed / satoshi_per_bitcoin);

                    return false;
                }

                if (++tmp.m_index < privateKeys.size())
                {
                    std::string request = R"({"method":"blockchain.scripthash.get_balance","params":[")" + generateScriptHash(privateKeys[tmp.m_index].to_public()) + R"("], "id": "teste"})";
                    request += "\n";

                    Result res = connection.m_stream->write(request.data(), request.size());
                    if (!res) {
                        LOG_ERROR() << error_str(res.error());
                    }
                    return true;
                }
            }
            callback(error, tmp.m_confirmed / satoshi_per_bitcoin);
            return false;
        });
    }

    void BitcoinElectrum::sendRequest(const std::string& method, const std::string& params, std::function<bool(const Error&, const json&, uint64_t)> callback)
    {
        std::string request(R"({"method":")" + method + R"(","params":[)" + params + R"(], "id": "test"})");
        request += "\n";

        LOG_INFO() << request;

        Address address;
        address.resolve("testnet1.bauerj.eu");
        address.port(50002);
        uint64_t currentTag = m_counter++;
        TCPConnect& connection = m_connections[currentTag];
        connection.m_request = request;
        connection.m_callback = callback;

        m_reactor.tcp_connect(address, currentTag, [this](uint64_t tag, std::unique_ptr<TcpStream>&& newStream, ErrorCode status)
        {
            if (newStream) {
                assert(status == EC_OK);
                TCPConnect& connection = m_connections[tag];

                connection.m_stream = std::move(newStream);

                connection.m_stream->enable_read([this, tag](ErrorCode what, void* data, size_t size) -> bool
                {
                    bool isFinished = true;
                    Error error{ None, "" };
                    json result;
                    {
                        if (size > 0 && data)
                        {
                            std::string strResponse = std::string(static_cast<const char*>(data), size);

                            try
                            {
                                json reply = json::parse(strResponse);

                                if (!reply["error"].empty())
                                {
                                    error.m_type = IBitcoinBridge::BitcoinError;
                                    error.m_message = reply["error"]["message"].get<std::string>();
                                }
                                else if (reply["result"].empty())
                                {
                                    error.m_type = IBitcoinBridge::EmptyResult;
                                    error.m_message = "JSON has no \"result\" value";
                                }
                                else
                                {
                                    result = reply["result"];
                                }
                            }
                            catch (const std::exception& ex)
                            {
                                error.m_type = IBitcoinBridge::InvalidResultFormat;
                                error.m_message = ex.what();
                            }
                        }
                        else
                        {
                            error.m_type = InvalidResultFormat;
                            error.m_message = "Empty response.";
                        }

                        TCPConnect& connection = m_connections[tag];
                        isFinished = !connection.m_callback(error, result, tag);
                    }
                    if (isFinished)
                    {
                        m_connections.erase(tag);
                        return false;
                    }
                    return true;
                });

                Result res = connection.m_stream->write(connection.m_request.data(), connection.m_request.size());
                if (!res) {
                    LOG_ERROR() << error_str(res.error());
                }
            }
            else
            {
                // error
            }
        }, 2000, true);
    }

    std::string BitcoinElectrum::getReceivingAddress(uint32_t index) const
    {
        ec_public publicKey(m_receivingPrivateKey.to_public().derive_public(index).point());        
        payment_address address = publicKey.to_payment_address(getAddressVersion());

        return address.encoded();
    }

    std::string BitcoinElectrum::getChangeAddress(uint32_t index) const
    {
        ec_public publicKey(m_changePrivateKey.to_public().derive_public(index).point());
        payment_address address = publicKey.to_payment_address(getAddressVersion());

        return address.encoded();
    }

    std::vector<libbitcoin::wallet::ec_private> BitcoinElectrum::generatePrivateKeyList() const
    {
        std::vector<ec_private> result;

        for (uint32_t i = 0; i < 21; i++)
        {
            result.push_back(ec_private(m_receivingPrivateKey.derive_private(i).secret(), getAddressVersion()));
        }

        for (uint32_t i = 0; i < 6; i++)
        {
            result.push_back(ec_private(m_changePrivateKey.derive_private(i).secret(), getAddressVersion()));
        }
        return result;
    }
}