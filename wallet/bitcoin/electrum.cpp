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
    std::string generateScriptHash(const ec_public& publicKey, uint8_t addressVersion)
    {
        payment_address addr = publicKey.to_payment_address(addressVersion);
        auto script = chain::script(chain::script::to_pay_key_hash_pattern(addr.hash()));
        libbitcoin::data_chunk secretHash = libbitcoin::sha256_hash_chunk(script.to_data(false));
        libbitcoin::data_chunk reverseSecretHash(secretHash.rbegin(), secretHash.rend());
        return libbitcoin::encode_base16(reverseSecretHash);
    }
}

namespace beam::bitcoin
{
    Electrum::Electrum(Reactor& reactor, IElectrumSettingsProvider::Ptr settingsProvider)
        : m_reactor(reactor)
        , m_settingsProvider(settingsProvider)
    {
    }

    void Electrum::dumpPrivKey(const std::string& btcAddress, std::function<void(const IBridge::Error&, const std::string&)> callback)
    {
        LOG_DEBUG() << "dumpPrivKey command";

        Error error{ None, "" };
        auto privateKeys = generateMasterPrivateKeys();
        auto settings = m_settingsProvider->GetElectrumSettings();
        auto addressVersion = settings.m_addressVersion;
        auto receivingAddressAmount = settings.m_receivingAddressAmount;
        for (uint32_t i = 0; i < receivingAddressAmount; ++i)
        {
            if (btcAddress == getAddress(i, privateKeys.first))
            {
                ec_private privateKey(privateKeys.first.derive_private(i).secret(), addressVersion);
                callback(error, privateKey.encoded());
                return;
            }
        }

        auto changeAddressAmount = settings.m_changeAddressAmount;
        for (uint32_t i = 0; i < changeAddressAmount; ++i)
        {
            if (btcAddress == getAddress(i, privateKeys.second))
            {
                ec_private privateKey(privateKeys.second.derive_private(i).secret(), addressVersion);
                callback(error, privateKey.encoded());
                return;
            }
        }

        error.m_type = ErrorType::BitcoinError;
        error.m_message = "This address is absent in wallet!";
        callback(error, "");
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
                hash_digest txHash;
                decode_hash(txHash, coin.m_details["tx_hash"].get<std::string>());
                unspentPoints.points.push_back(point_value(point(txHash, coin.m_details["tx_pos"].get<uint32_t>()), coin.m_details["value"].get<uint64_t>()));
            }

            auto privateKeys = generateMasterPrivateKeys();
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
                    payment_address destinationAddress(getAddress(0, privateKeys.second));
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
                        script lockingScript = script().to_pay_key_hash_pattern(coin.m_privateKey.to_public().to_payment_address(m_settingsProvider->GetElectrumSettings().m_addressVersion).hash());
                        endorsement sig;
                        if (lockingScript.create_endorsement(sig, coin.m_privateKey.secret(), lockingScript, tx, static_cast<uint32_t>(ind), machine::sighash_algorithm::all))
                        {
                            script::operation::list sigScript;
                            sigScript.push_back(script::operation(sig));
                            data_chunk tmp;
                            coin.m_privateKey.to_public().to_data(tmp);
                            sigScript.push_back(script::operation(tmp));
                            script unlockingScript(sigScript);

                            tx.inputs()[ind].set_script(unlockingScript);
                        }
                        else
                        {
                            callback({IBridge::BitcoinError, "Transaction is not signed"}, "", false);
                            return;
                        }
                        break;
                    }
                }

                if (!isFoundCoin)
                {
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
        auto privateKeys = generateMasterPrivateKeys();
        callback(error, getAddress(m_currentReceivingAddress++, privateKeys.first));

        auto receivingAddressAmount = m_settingsProvider->GetElectrumSettings().m_receivingAddressAmount;
        if (m_currentReceivingAddress >= receivingAddressAmount)
        {
            m_currentReceivingAddress = 0;
        }
    }

    void Electrum::createRawTransaction(
        const std::string& withdrawAddress,
        const std::string& contractTxId,
        uint64_t amount,
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

    void Electrum::getTxOut(const std::string& txid, int outputIndex, std::function<void(const IBridge::Error&, const std::string&, double, uint32_t)> callback)
    {
        //LOG_DEBUG() << "getTxOut command";
        sendRequest("blockchain.transaction.get", "\"" + txid + "\", true", [callback, outputIndex](IBridge::Error error, const json& result, uint64_t)
        {
            double value = 0;
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
                            value = vout["value"].get<double>();
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
                    blockCount = result["height"].get<uint64_t>();
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
    void Electrum::getBalance(uint32_t confirmations, std::function<void(const Error&, double)> callback)
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

                double balance = 0;
                for (auto coin : coins)
                {
                    if (coin.m_details["height"].get<uint64_t>() <= height)
                    {
                        balance += coin.m_details["value"].get<uint64_t>();
                    }
                }

                callback(error, balance);
            });
        });
    }

    void Electrum::getDetailedBalance(std::function<void(const Error&, double, double, double)> callback)
    {
        LOG_DEBUG() << "getDetailedBalance command";

        size_t index = 0;
        double confirmed = 0;
        double unconfirmed = 0;
        auto privateKeys = generatePrivateKeyList();
        auto addressVersion = m_settingsProvider->GetElectrumSettings().m_addressVersion;

        sendRequest("blockchain.scripthash.get_balance", "\"" + generateScriptHash(privateKeys[0].to_public(), addressVersion) + "\"",
            [this, callback, index, confirmed, unconfirmed, privateKeys, addressVersion](IBridge::Error error, const json& result, uint64_t tag) mutable
        {
            if (error.m_type == IBridge::None)
            {
                TCPConnect& connection = m_connections[tag];

                try
                {
                    confirmed += result["confirmed"].get<double>();
                    unconfirmed += result["unconfirmed"].get<double>();
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBridge::InvalidResultFormat;
                    error.m_message = ex.what();

                    callback(error, confirmed / satoshi_per_bitcoin, unconfirmed / satoshi_per_bitcoin, 0);

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
            callback(error, confirmed / satoshi_per_bitcoin, unconfirmed / satoshi_per_bitcoin, 0);
            return false;
        });
    }

    void Electrum::listUnspent(std::function<void(const Error&, const std::vector<Utxo>&)> callback)
    {
        LOG_DEBUG() << "listunstpent command";
        size_t index = 0;
        std::vector<Utxo> coins;
        auto privateKeys = generatePrivateKeyList();
        auto addressVersion = m_settingsProvider->GetElectrumSettings().m_addressVersion;

        sendRequest("blockchain.scripthash.listunspent", "\"" + generateScriptHash(privateKeys[0].to_public(), addressVersion) + "\"",
            [this, callback, index, coins, privateKeys, addressVersion](IBridge::Error error, const json& result, uint64_t tag) mutable
        {
            if (error.m_type == IBridge::None || error.m_type == IBridge::EmptyResult)
            {
                TCPConnect& connection = m_connections[tag];

                {
                    payment_address addr(privateKeys[index].to_public().to_payment_address(addressVersion));
                    LOG_INFO() << "address = " << addr.encoded();
                }
                try
                {
                    for (auto utxo : result)
                    {
                        Utxo coin;
                        coin.m_privateKey = privateKeys[index];
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
                IBridge::Error emptyError{ ErrorType::None, "" };
                callback(emptyError, coins);
                return false;
            }
            callback(error, coins);
            return false;
        });
    }

    void Electrum::sendRequest(const std::string& method, const std::string& params, std::function<bool(const Error&, const json&, uint64_t)> callback)
    {
        std::string request(R"({"method":")" + method + R"(","params":[)" + params + R"(], "id": "test"})");
        request += "\n";

        //LOG_INFO() << request;

        uint64_t currentTag = m_tagCounter++;
        TCPConnect& connection = m_connections[currentTag];
        connection.m_request = request;
        connection.m_callback = callback;

        m_reactor.tcp_connect(m_settingsProvider->GetElectrumSettings().m_address, currentTag, [this](uint64_t tag, std::unique_ptr<TcpStream>&& newStream, ErrorCode status)
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

    std::string Electrum::getAddress(uint32_t index, const hd_private& privateKey) const
    {
        ec_public publicKey(privateKey.to_public().derive_public(index).point());
        payment_address address = publicKey.to_payment_address(m_settingsProvider->GetElectrumSettings().m_addressVersion);

        return address.encoded();
    }    

    std::vector<libbitcoin::wallet::ec_private> Electrum::generatePrivateKeyList() const
    {
        std::vector<ec_private> result;
        auto privateKeys = generateMasterPrivateKeys();
        auto settings = m_settingsProvider->GetElectrumSettings();
        auto addressVersion = settings.m_addressVersion;
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

    std::pair<hd_private, hd_private> Electrum::generateMasterPrivateKeys() const
    {
        word_list seedPhrase(m_settingsProvider->GetElectrumSettings().m_secretWords);
        auto hd_seed = electrum::decode_mnemonic(seedPhrase);
        data_chunk seed_chunk(to_chunk(hd_seed));
        hd_private masterPrivateKey(seed_chunk,
            m_settingsProvider->GetElectrumSettings().m_isMainnet ? hd_public::mainnet : hd_public::testnet);

        return std::make_pair(masterPrivateKey.derive_private(0), masterPrivateKey.derive_private(1));
    }
} // namespace beam::bitcoin