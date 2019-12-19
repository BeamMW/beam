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

#include "electrum.h"


#include "utility/io/reactor.h"
#include "utility/io/timer.h"
#include "utility/io/tcpstream.h"
#include "utility/config.h"
#include "utility/helpers.h"
#include "utility/logger.h"

#include "common.h"

#include "bitcoin/bitcoin.hpp"

using json = nlohmann::json;
using namespace beam;
using namespace beam::io;
using namespace libbitcoin;
using namespace libbitcoin::wallet;
using namespace libbitcoin::chain;

namespace
{
    const std::chrono::seconds kRequestPeriod = std::chrono::seconds(10);
    std::string generateScriptHash(const ec_public& publicKey, uint8_t addressVersion)
    {
        payment_address addr = publicKey.to_payment_address(addressVersion);
        auto script = chain::script(chain::script::to_pay_key_hash_pattern(addr.hash()));
        libbitcoin::data_chunk secretHash = libbitcoin::sha256_hash_chunk(script.to_data(false));
        libbitcoin::data_chunk reverseSecretHash(secretHash.rbegin(), secretHash.rend());
        return libbitcoin::encode_base16(reverseSecretHash);
    }

    const char kInvalidGenesisBlockHashMsg[] = "Invalid genesis block hash";
}

namespace beam::bitcoin
{
    Electrum::Electrum(Reactor& reactor, ISettingsProvider& settingsProvider)
        : m_reactor(reactor)
        , m_settingsProvider(settingsProvider)
    {
        auto settings = m_settingsProvider.GetSettings();
        auto electrumSettings = settings.GetElectrumConnectionOptions();
        auto idx = std::find(electrumSettings.m_nodeAddresses.begin(), electrumSettings.m_nodeAddresses.end(), electrumSettings.m_address);

        if (idx != electrumSettings.m_nodeAddresses.end())
        {
            m_currentAddressIndex = std::distance(electrumSettings.m_nodeAddresses.begin(), idx);
        }
    }

    Electrum::~Electrum()
    {
        for (const auto& connection : m_connections)
        {
            auto tag = uint64_t(&connection.second);
            m_reactor.cancel_tcp_connect(tag);
        }
    }

    void Electrum::fundRawTransaction(const std::string& rawTx, Amount feeRate, std::function<void(const IBridge::Error&, const std::string&, int)> callback)
    {
        LOG_DEBUG() << "fundRawTransaction command";

        listUnspent([this, rawTx, feeRate, callback](const IBridge::Error& error, const std::vector<Electrum::Utxo>& coins)
        {
            if (error.m_type != ErrorType::None)
            {
                callback(error, "", 0);
                return;
            }

            reviewLockedUtxo();

            data_chunk txData;
            decode_base16(txData, rawTx);
            transaction tx;
            tx.from_data_without_inputs(txData);
            uint64_t total = 0;
            for (auto o : tx.outputs())
            {
                total += o.value();
            }

            points_value unspentPoints;
            for (auto coin : coins)
            {
                if (isLockedUtxo(coin.m_details["tx_hash"].get<std::string>(), coin.m_details["tx_pos"].get<uint32_t>()))
                    continue;
                hash_digest txHash;
                decode_hash(txHash, coin.m_details["tx_hash"].get<std::string>());
                unspentPoints.points.push_back(point_value(point(txHash, coin.m_details["tx_pos"].get<uint32_t>()), coin.m_details["value"].get<uint64_t>()));
            }

            auto privateKeys = generateElectrumMasterPrivateKeys(m_settingsProvider.GetSettings().GetElectrumConnectionOptions().m_secretWords);
            while (true)
            {
                int changePosition = -1;
                points_value resultPoints;

                select_outputs::select(resultPoints, unspentPoints, total);

                if (resultPoints.value() < total)
                {
                    IBridge::Error internalError{ErrorType::BitcoinError, "not enough coins"};
                    callback(internalError, "", 0);
                    return;
                }

                transaction newTx(tx);
                newTx.set_version(kTransactionVersion);

                uint64_t totalInputValue = 0;
                for (auto p : resultPoints.points)
                {
                    input in;
                    output_point outputPoint(p.hash(), p.index());
                    totalInputValue += p.value();
                    in.set_previous_output(outputPoint);
                    newTx.inputs().push_back(in);
                }
                auto weight = newTx.weight();

                Amount fee = static_cast<Amount>(std::round(double(weight * feeRate) / 1000));
                auto newTxFee = totalInputValue - newTx.total_output_value();

                if (fee > newTxFee)
                {
                    total += fee;
                    continue;
                }

                if (fee < newTxFee)
                {
                    payment_address destinationAddress(getElectrumAddress(privateKeys.second, 0, m_settingsProvider.GetSettings().GetAddressVersion()));
                    script outputScript = script().to_pay_key_hash_pattern(destinationAddress.hash());
                    output out(newTxFee - fee, outputScript);
                    Amount feeOutput = static_cast<Amount>(std::round(double(out.serialized_size() * feeRate) / 1000));

                    if (fee + feeOutput < newTxFee)
                    {
                        out.set_value(newTxFee - (fee + feeOutput));
                        if (!out.is_dust(kDustThreshold))
                        {
                            newTx.outputs().push_back(out);
                            changePosition = static_cast<int>(newTx.outputs().size()) - 1;
                        }
                    }
                }

                for (auto p : resultPoints.points)
                {
                    lockUtxo(encode_hash(p.hash()), p.index());
                }

                callback(error, encode_base16(newTx.to_data()), changePosition);
                return;
            }
        });
    }

    void Electrum::signRawTransaction(const std::string& rawTx, std::function<void(const IBridge::Error&, const std::string&, bool)> callback)
    {
        LOG_DEBUG() << "signRawTransaction command";

        listUnspent([this, rawTx, callback](const IBridge::Error& error, const std::vector<Electrum::Utxo>& coins)
        {
            if (error.m_type != ErrorType::None)
            {
                callback(error, "", 0);
                return;
            }

            auto privateKeys = generatePrivateKeyList();
            data_chunk txData;
            decode_base16(txData, rawTx);
            transaction tx = transaction::factory_from_data(txData);

            for (size_t ind = 0; ind < tx.inputs().size(); ++ind)
            {
                // TODO roman.strilets should be tested without inputs
                auto previousOutput = tx.inputs()[ind].previous_output();
                auto strHash = encode_hash(previousOutput.hash());
                auto index = previousOutput.index();
                bool isFoundCoin = false;

                for (auto coin : coins)
                {
                    if (coin.m_details["tx_hash"].get<std::string>() == strHash && coin.m_details["tx_pos"].get<uint32_t>() == index)
                    {
                        isFoundCoin = true;
                        ec_private privateKey = privateKeys[coin.m_index];
                        script lockingScript = script().to_pay_key_hash_pattern(privateKey.to_public().to_payment_address(m_settingsProvider.GetSettings().GetAddressVersion()).hash());
                        endorsement sig;
                        if (lockingScript.create_endorsement(sig, privateKey.secret(), lockingScript, tx, static_cast<uint32_t>(ind), machine::sighash_algorithm::all))
                        {
                            script::operation::list sigScript;
                            sigScript.push_back(script::operation(sig));
                            data_chunk tmp;
                            privateKey.to_public().to_data(tmp);
                            sigScript.push_back(script::operation(tmp));
                            script unlockingScript(sigScript);

                            tx.inputs()[ind].set_script(unlockingScript);
                        }
                        else
                        {
                            LOG_DEBUG() << "signRawTransaction command: failed in lockingScript.create_endorsement";
                            callback({IBridge::BitcoinError, "Transaction is not signed"}, "", false);
                            return;
                        }
                        break;
                    }
                }

                if (!isFoundCoin)
                {
                    LOG_DEBUG() << "signRawTransaction command: coin is not found";
                    callback({ IBridge::BitcoinError, "Transaction is not signed" }, "", false);
                    return;
                }
            }

            if (!tx.is_valid())
            {
                callback({ IBridge::BitcoinError, "Transaction is not valid" }, "", false);
                return;
            }

            callback(error, encode_base16(tx.to_data()), true);
        });
    }

    void Electrum::sendRawTransaction(const std::string& rawTx, std::function<void(const IBridge::Error&, const std::string&)> callback)
    {
        LOG_DEBUG() << "sendRawTransaction command";

        sendRequest("blockchain.transaction.broadcast", "\"" + rawTx + "\"", [callback](IBridge::Error error, const json& result, uint64_t)
        {
            std::string txID;

            if (error.m_type == IBridge::None)
            {
                try
                {
                    txID = result.get<std::string>();
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }

            callback(error, txID);
            return false;
        });
    }

    void Electrum::getRawChangeAddress(std::function<void(const IBridge::Error&, const std::string&)> callback)
    {
        LOG_DEBUG() << "getRawChangeAddress command";

        Error error{ None, "" };
        auto privateKeys = generateElectrumMasterPrivateKeys(m_settingsProvider.GetSettings().GetElectrumConnectionOptions().m_secretWords);
        std::srand(static_cast<unsigned int>(std::time(0)));
        uint32_t index = static_cast<uint32_t>(std::rand() % m_settingsProvider.GetSettings().GetElectrumConnectionOptions().m_receivingAddressAmount);

        callback(error, getElectrumAddress(privateKeys.first, index, m_settingsProvider.GetSettings().GetAddressVersion()));
    }

    void Electrum::createRawTransaction(
        const std::string& withdrawAddress,
        const std::string& contractTxId,
        Amount amount,
        int outputIndex,
        Timestamp locktime,
        std::function<void(const IBridge::Error&, const std::string&)> callback)
    {
        LOG_DEBUG() << "createRawTransaction command";
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

        tx.set_version(kTransactionVersion);

        Error error{ None, "" };
        callback(error, encode_base16(tx.to_data()));
    }

    void Electrum::getTxOut(const std::string& txid, int outputIndex, std::function<void(const IBridge::Error&, const std::string&, Amount, uint32_t)> callback)
    {
        //LOG_DEBUG() << "getTxOut command";
        sendRequest("blockchain.transaction.get", "\"" + txid + "\", true", [callback, outputIndex](IBridge::Error error, const json& result, uint64_t)
        {
            Amount value = 0;
            uint32_t confirmations = 0;
            std::string scriptHex;

            if (error.m_type == IBridge::None)
            {
                try
                {
                    if (result.find("confirmations") != result.end())
                    {
                        confirmations = result["confirmations"].get<uint16_t>();
                    }

                    bool isFind = false;

                    for (const auto& vout : result["vout"])
                    {
                        if (vout["n"].get<int>() == outputIndex)
                        {
                            scriptHex = vout["scriptPubKey"]["hex"].get<std::string>();
                            // TODO should avoid using of double type
                            value = btc_to_satoshi(vout["value"].get<double>());
                            isFind = true;
                            break;
                        }
                    }

                    if (!isFind)
                    {
                        error.m_type = IBridge::BitcoinError;
                        error.m_message = "Output is absent!";
                    }
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }

            callback(error, scriptHex, value, confirmations);
            return false;
        });
    }

    void Electrum::getBlockCount(std::function<void(const IBridge::Error&, uint64_t)> callback)
    {
        //LOG_DEBUG() << "getBlockCount command";

        sendRequest("blockchain.headers.subscribe", "", [callback](IBridge::Error error, const json& result, uint64_t)
        {
            uint64_t blockCount = 0;

            if (error.m_type == IBridge::None)
            {
                try
                {
                    auto key = (result.find("height") != result.end()) ? "height" : "block_height";

                    blockCount = result[key].get<uint64_t>();
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }
            callback(error, blockCount);
            return false;
        });
    }

    // TODO roman.strilets check this implementation
    void Electrum::getBalance(uint32_t confirmations, std::function<void(const Error&, Amount)> callback)
    {
        getBlockCount([confirmations, callback, this](const Error& error, uint64_t height)
        {
            if (error.m_type != ErrorType::None)
            {
                callback(error, 0);
                return;
            }

            if (confirmations > height)
            {
                Error err{ IBridge::BitcoinError, std::string("Height should be more than confirmations") };
                callback(err, 0);
                return;
            }
            
            listUnspent([height = height - confirmations + 1, callback](const Error& error, const std::vector<Utxo>& coins) 
            {
                if (error.m_type != ErrorType::None)
                {
                    callback(error, 0);
                    return;
                }

                Amount balance = 0;
                for (auto coin : coins)
                {
                    if (coin.m_details["height"].get<uint64_t>() <= height)
                    {
                        balance += coin.m_details["value"].get<Amount>();
                    }
                }

                callback(error, balance);
            });
        });
    }

    void Electrum::getDetailedBalance(std::function<void(const Error&, Amount, Amount, Amount)> callback)
    {
        //LOG_DEBUG() << "getDetailedBalance command";

        size_t index = 0;
        Amount confirmed = 0;
        Amount unconfirmed = 0;
        auto privateKeys = generatePrivateKeyList();
        auto addressVersion = m_settingsProvider.GetSettings().GetAddressVersion();

        sendRequest("blockchain.scripthash.get_balance", "\"" + generateScriptHash(privateKeys[0].to_public(), addressVersion) + "\"",
            [this, callback, index, confirmed, unconfirmed, privateKeys, addressVersion](IBridge::Error error, const json& result, uint64_t tag) mutable
        {
            if (error.m_type == IBridge::None)
            {
                TCPConnect& connection = m_connections[tag];

                try
                {
                    confirmed += result["confirmed"].get<Amount>();
                    unconfirmed += result["unconfirmed"].get<Amount>();
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBridge::InvalidResultFormat;
                    error.m_message = ex.what();

                    callback(error, confirmed, unconfirmed, 0);

                    return false;
                }

                if (++index < privateKeys.size())
                {
                    std::string request = R"({"method":"blockchain.scripthash.get_balance","params":[")" + generateScriptHash(privateKeys[index].to_public(), addressVersion) + R"("], "id": "teste"})";
                    request += "\n";

                    Result res = connection.m_stream->write(request.data(), request.size());
                    if (!res) {
                        LOG_ERROR() << error_str(res.error());
                    }
                    return true;
                }
            }
            callback(error, confirmed, unconfirmed, 0);
            return false;
        });
    }

    void Electrum::getGenesisBlockHash(std::function<void(const Error&, const std::string&)> callback)
    {
        sendRequest("server.features", "", [callback](IBridge::Error error, const json& result, uint64_t)
            {
                std::string genesisBlockHash;

                if (error.m_type == IBridge::None)
                {
                    try
                    {
                        genesisBlockHash = result["genesis_hash"].get<std::string>();
                    }
                    catch (const std::exception & ex)
                    {
                        error.m_type = IBridge::InvalidResultFormat;
                        error.m_message = ex.what();
                    }
                }
                callback(error, genesisBlockHash);
                return false;
            });
    }

    void Electrum::listUnspent(std::function<void(const Error&, const std::vector<Utxo>&)> callback)
    {
        LOG_DEBUG() << "listunstpent command";
        size_t index = 0;
        std::vector<Utxo> coins;
        auto privateKeys = generatePrivateKeyList();
        auto addressVersion = m_settingsProvider.GetSettings().GetAddressVersion();

        if (m_cache.empty() || (std::chrono::system_clock::now() - m_lastCache > kRequestPeriod))
        {
            sendRequest("blockchain.scripthash.listunspent", "\"" + generateScriptHash(privateKeys[0].to_public(), addressVersion) + "\"",
                [this, callback, index, coins, privateKeys, addressVersion](IBridge::Error error, const json& result, uint64_t tag) mutable
            {
                if (error.m_type == IBridge::None || error.m_type == IBridge::EmptyResult)
                {
                    TCPConnect& connection = m_connections[tag];

                    try
                    {
                        for (auto utxo : result)
                        {
                            Utxo coin;
                            coin.m_index = index;
                            coin.m_details = utxo;
                            coins.push_back(coin);
                        }
                    }
                    catch (const std::exception& ex)
                    {
                        error.m_type = IBridge::InvalidResultFormat;
                        error.m_message = ex.what();

                        callback(error, coins);

                        return false;
                    }

                    if (++index < privateKeys.size())
                    {
                        std::string request = R"({"method":"blockchain.scripthash.listunspent","params":[")" + generateScriptHash(privateKeys[index].to_public(), addressVersion) + R"("], "id": "teste"})";
                        request += "\n";

                        Result res = connection.m_stream->write(request.data(), request.size());
                        if (!res) {
                            LOG_ERROR() << error_str(res.error());
                        }
                        return true;
                    }
                    m_lastCache = std::chrono::system_clock::now();
                    m_cache = coins;
                    IBridge::Error emptyError{ ErrorType::None, "" };
                    callback(emptyError, coins);
                    return false;
                }
                callback(error, coins);
                return false;
            });
        }
        else
        {
            m_asyncEvent = io::AsyncEvent::create(io::Reactor::get_Current(), [callback, cache = m_cache]()
            {
                Error error{ None, "" };
                callback(error, cache);
            });
            m_asyncEvent->post();
        }
    }

    void Electrum::sendRequest(const std::string& method, const std::string& params, std::function<bool(const Error&, const json&, uint64_t)> callback)
    {
        std::string request(R"({"method":")" + method + R"(","params":[)" + params + R"(], "id": "test"})");
        request += "\n";

        auto settings = m_settingsProvider.GetSettings();
        //LOG_INFO() << request;
        io::Address address;
        {
            auto electrumSettings = settings.GetElectrumConnectionOptions();
            
            if (electrumSettings.m_automaticChooseAddress && m_currentAddressIndex < electrumSettings.m_nodeAddresses.size() &&
                electrumSettings.m_address != electrumSettings.m_nodeAddresses.at(m_currentAddressIndex))
            {
                electrumSettings.m_address = electrumSettings.m_nodeAddresses.at(m_currentAddressIndex);

                settings.SetElectrumConnectionOptions(electrumSettings);
                m_settingsProvider.SetSettings(settings);
            }


            if (!address.resolve(electrumSettings.m_address.c_str()))
            {
                tryToChangeAddress();

                LOG_ERROR() << "unable to resolve electrum address: " << electrumSettings.m_address;

                // TODO maybe to need async??
                Error error{ IOError, "unable to resolve electrum address: " + electrumSettings.m_address };
                json result;
                callback(error, result, 0);
                return;
            }
        }

        uint64_t currentId = m_idCounter++;
        TCPConnect& connection = m_connections[currentId];
        connection.m_request = request;
        connection.m_callback = callback;

        auto tag = uint64_t(&connection);
        auto result = m_reactor.tcp_connect(address, tag, [this, currentId, weak = this->weak_from_this(), settings](uint64_t tag, std::unique_ptr<TcpStream>&& newStream, ErrorCode status)
        {
            if (weak.expired())
            {
                return;
            }

            TCPConnect& connection = m_connections[currentId];

            if (newStream) {
                assert(status == EC_OK);

                connection.m_stream = std::move(newStream);

                connection.m_stream->enable_read([this, weak, currentId, settings](ErrorCode what, void* data, size_t size) -> bool
                {
                    if (weak.expired())
                    {
                        return false;
                    }

                    bool isFinished = true;
                    Error error{ None, "" };
                    json result;
                    {
                        if (size > 0 && data)
                        {
                            std::string strResponse = std::string(static_cast<const char*>(data), size);

                            //LOG_INFO() << "strResponse: " << strResponse;
                            try
                            {
                                json reply = json::parse(strResponse);

                                if (!reply["error"].empty())
                                {
                                    error.m_type = IBridge::BitcoinError;
                                    error.m_message = reply["error"]["message"].get<std::string>();
                                }
                                else if (reply["result"].empty())
                                {
                                    error.m_type = IBridge::EmptyResult;
                                    error.m_message = "JSON has no \"result\" value";
                                }
                                else
                                {
                                    result = reply["result"];
                                    auto id = reply["id"].get<std::string>();
                                    if (id == "verify")
                                    {
                                        auto genesisBlockHash = result["genesis_hash"].get<std::string>();
                                        TCPConnect& connection = m_connections[currentId];
                                        auto genesisBlockHashes = settings.GetGenesisBlockHashes();
                                        auto currentNodeAddress = settings.GetElectrumConnectionOptions().m_address;

                                        if (std::find(genesisBlockHashes.begin(), genesisBlockHashes.end(), genesisBlockHash) != genesisBlockHashes.end())
                                        {
                                            m_verifiedAddresses.emplace(currentNodeAddress, true);
                                            Result res = connection.m_stream->write(connection.m_request.data(), connection.m_request.size());
                                            if (!res)
                                            {
                                                LOG_ERROR() << error_str(res.error());
                                            }
                                            return true;
                                        }
                                        else
                                        {
                                            m_verifiedAddresses.emplace(currentNodeAddress, false);

                                            tryToChangeAddress();

                                            error.m_message = kInvalidGenesisBlockHashMsg;
                                            error.m_type = IBridge::InvalidGenesisBlock;
                                            connection.m_callback(error, result, currentId);
                                            return false;
                                        }
                                    }
                                }
                            }
                            catch (const std::exception& ex)
                            {
                                error.m_type = IBridge::InvalidResultFormat;
                                error.m_message = ex.what();
                            }
                        }
                        else
                        {
                            error.m_type = IOError;
                            error.m_message = "Empty response.";
                        }

                        TCPConnect& connection = m_connections[currentId];
                        isFinished = !connection.m_callback(error, result, currentId);
                    }
                    if (isFinished)
                    {
                        m_connections.erase(currentId);
                        return false;
                    }
                    return true;
                });

                Result res;

                // find address in map
                auto iter = m_verifiedAddresses.find(settings.GetElectrumConnectionOptions().m_address);
                if (iter != m_verifiedAddresses.end())
                {
                    // node have invalid genesis block hash
                    if (!iter->second)
                    {
                        tryToChangeAddress();
                        // error
                        Error error{ InvalidGenesisBlock, kInvalidGenesisBlockHashMsg };
                        json result;
                        connection.m_callback(error, result, currentId);
                        m_connections.erase(currentId);
                        return;
                    }
                    res = connection.m_stream->write(connection.m_request.data(), connection.m_request.size());
                }
                else
                {
                    // Node have not validated yet
                    std::string verifyRequest = R"({"method":"server.features","params":[], "id": "verify"})";
                    verifyRequest += "\n";
                    res = connection.m_stream->write(verifyRequest.data(), verifyRequest.size());
                }

                if (!res) {
                    LOG_ERROR() << error_str(res.error());
                }
            }
            else
            {
                tryToChangeAddress();

                // error
                Error error{ IOError, "stream is empty" };
                json result;
                connection.m_callback(error, result, currentId);
                m_connections.erase(currentId);
            }
        }, 2000, true);

        if (result)
            return;

        LOG_ERROR() << "error in Electrum::sendRequest: code = " << io::error_descr(result.error());
    }

    std::vector<libbitcoin::wallet::ec_private> Electrum::generatePrivateKeyList() const
    {
        std::vector<ec_private> result;
        auto settings = m_settingsProvider.GetSettings().GetElectrumConnectionOptions();
        auto privateKeys = generateElectrumMasterPrivateKeys(settings.m_secretWords);
        auto addressVersion = m_settingsProvider.GetSettings().GetAddressVersion();
        auto receivingAddressAmount = settings.m_receivingAddressAmount;

        for (uint32_t i = 0; i < receivingAddressAmount; i++)
        {
            result.push_back(ec_private(privateKeys.first.derive_private(i).secret(), addressVersion));
        }

        auto changeAddressAmount = settings.m_changeAddressAmount;
        for (uint32_t i = 0; i < changeAddressAmount; i++)
        {
            result.push_back(ec_private(privateKeys.second.derive_private(i).secret(), addressVersion));
        }
        return result;
    }

    void Electrum::lockUtxo(std::string hash, uint32_t pos)
    {
        LockUtxo utxo{ hash, pos, std::chrono::system_clock::now() };
        m_lockedUtxo.push_back(utxo);
    }

    bool Electrum::isLockedUtxo(std::string hash, uint32_t pos)
    {
        return std::find_if(m_lockedUtxo.begin(), m_lockedUtxo.end(), [hash, pos](const LockUtxo& u) -> bool
        {
            return u.m_txHash == hash && u.m_pos == pos;
        }) != m_lockedUtxo.end();
    }

    void Electrum::reviewLockedUtxo()
    {
        auto idx = std::remove_if(m_lockedUtxo.begin(), m_lockedUtxo.end(), [now = std::chrono::system_clock::now()](const LockUtxo& u) -> bool
        {
            return (now - u.m_time) > std::chrono::hours(2);
        });

        m_lockedUtxo.erase(idx, m_lockedUtxo.end());
    }

    void Electrum::tryToChangeAddress()
    {
        auto settings = m_settingsProvider.GetSettings();
        auto electrumSettings = settings.GetElectrumConnectionOptions();

        if (electrumSettings.m_automaticChooseAddress && m_currentAddressIndex < electrumSettings.m_nodeAddresses.size())
        {
            auto index = m_currentAddressIndex;
            if (electrumSettings.m_address == electrumSettings.m_nodeAddresses.at(m_currentAddressIndex))
            {
                do {
                    ++m_currentAddressIndex;
                    if (m_currentAddressIndex >= electrumSettings.m_nodeAddresses.size())
                    {
                        m_currentAddressIndex = 0;
                    }
                } while (index != m_currentAddressIndex &&
                    (electrumSettings.m_address == electrumSettings.m_nodeAddresses.at(m_currentAddressIndex) ||
                        !isNodeAddressCheckedAndVerified(electrumSettings.m_nodeAddresses.at(m_currentAddressIndex))));
            }

            if (index != m_currentAddressIndex)
            {
                electrumSettings.m_address = electrumSettings.m_nodeAddresses.at(m_currentAddressIndex);
                settings.SetElectrumConnectionOptions(electrumSettings);
                m_settingsProvider.SetSettings(settings);
            }
        }
    }

    bool Electrum::isNodeAddressCheckedAndVerified(const std::string& address) const
    {
        auto iter = m_verifiedAddresses.find(address);
        return iter == m_verifiedAddresses.end() || (iter != m_verifiedAddresses.end() && iter->second);
    }
} // namespace beam::bitcoin