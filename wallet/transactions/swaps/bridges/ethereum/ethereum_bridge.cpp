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

#include "ethereum_bridge.h"

#include "common.h"

#include "utility/logger.h"
#include "nlohmann/json.hpp"

#include <ethash/keccak.hpp>
#include "utility/hex.h"

#include <boost/format.hpp>

using json = nlohmann::json;

namespace
{
libbitcoin::wallet::hd_private ProcessHDPrivate(const libbitcoin::wallet::hd_private& privateKey, uint32_t index, bool hard = true)
{
    static constexpr auto first = libbitcoin::wallet::hd_first_hardened_key;
    const auto position = hard ? first + index : index;
    return privateKey.derive_private(position);
}

bool needSsl(const std::string& address)
{
    // TODO roman.strilets need insensitive
    return address.find("infura") != std::string::npos;
}
}

namespace beam::ethereum
{
EthereumBridge::EthereumBridge(io::Reactor& reactor, ISettingsProvider& settingsProvider)
    : m_httpClient(reactor, needSsl(settingsProvider.GetSettings().m_address))
    , m_settingsProvider(settingsProvider)
    , m_txConfirmationTimer(io::Timer::create(reactor))
{
}

void EthereumBridge::getBalance(std::function<void(const Error&, const std::string&)> callback)
{
    LOG_DEBUG() << "EthereumBridge::getBalance";
    std::string ethAddress = ConvertEthAddressToStr(generateEthAddress());
    std::string params = (boost::format(R"("%1%","latest")") % ethAddress).str();

    sendRequest("eth_getBalance", params, [callback](Error error, const json& result)
    {
        LOG_DEBUG() << "EthereumBridge::getBalance in";
        std::string balance = "";

        if (error.m_type == IBridge::None)
        {
            try
            {
                balance = result["result"].get<std::string>();
            }
            catch (const std::exception& ex)
            {
                error.m_type = IBridge::InvalidResultFormat;
                error.m_message = ex.what();
            }
        }
        callback(error, balance);
    });
}

void EthereumBridge::getTokenBalance(
    const std::string& contractAddr,
    std::function<void(const Error&, const std::string&)> callback)
{
    LOG_DEBUG() << "EthereumBridge::getTokenBalance";
    const auto tokenContractAddress = ethereum::ConvertStrToEthAddress(contractAddr);

    libbitcoin::data_chunk data;
    data.reserve(ethereum::kEthContractMethodHashSize + ethereum::kEthContractABIWordSize);
    libbitcoin::decode_base16(data, ethereum::ERC20Hashes::kBalanceOfHash);
    ethereum::AddContractABIWordToBuffer(generateEthAddress(), data);

    call(tokenContractAddress, libbitcoin::encode_base16(data), [callback](const IBridge::Error& err, const nlohmann::json& result)
    {
        LOG_DEBUG() << "EthereumBridge::getTokenBalance in";
        Error error = err;
        std::string balance = "";

        if (error.m_type == IBridge::None)
        {
            try
            {
                balance = result.get<std::string>();
            }
            catch (const std::exception& ex)
            {
                error.m_type = IBridge::InvalidResultFormat;
                error.m_message = ex.what();
            }
        }
        callback(error, balance);
    });
}

void EthereumBridge::getBlockNumber(std::function<void(const Error&, uint64_t)> callback)
{
    LOG_DEBUG() << "EthereumBridge::getBlockNumber";
    sendRequest("eth_blockNumber", "", [callback](Error error, const json& result)
    {
        LOG_DEBUG() << "EthereumBridge::getBlockNumber in";
        uint64_t blockNumber = 0;

        if (error.m_type == IBridge::None)
        {
            try
            {
                std::string strBlockNumber = result["result"].get<std::string>();
                blockNumber = std::stoull(strBlockNumber, nullptr, 16);
            }
            catch (const std::exception& ex)
            {
                error.m_type = IBridge::InvalidResultFormat;
                error.m_message = ex.what();
            }
        }
        callback(error, blockNumber);
    });
}

void EthereumBridge::getTransactionCount(std::function<void(const Error&, uint64_t)> callback)
{
    LOG_DEBUG() << "EthereumBridge::getTransactionCount";
    std::string ethAddress = ConvertEthAddressToStr(generateEthAddress());
    std::string params = (boost::format(R"("%1%","pending")") % ethAddress).str();

    sendRequest("eth_getTransactionCount", params, [callback](Error error, const json& result)
    {
        LOG_DEBUG() << "EthereumBridge::getTransactionCount in";
        uint64_t txCount = 0;

        if (error.m_type == IBridge::None)
        {
            try
            {
                std::string st = result["result"].get<std::string>();
                txCount = std::stoull(st, nullptr, 16);
            }
            catch (const std::exception& ex)
            {
                error.m_type = IBridge::InvalidResultFormat;
                error.m_message = ex.what();
            }
        }
        callback(error, txCount);
    });
}

void EthereumBridge::sendRawTransaction(const std::string& rawTx, std::function<void(const Error&, std::string)> callback)
{
    LOG_DEBUG() << "EthereumBridge::sendRawTransaction";
    std::string params = (boost::format(R"("%1%")") % AddHexPrefix(rawTx)).str();

    sendRequest("eth_sendRawTransaction", params, [callback](Error error, const json& result)
    {
        LOG_DEBUG() << "EthereumBridge::sendRawTransaction in";
        std::string txHash = "";

        if (error.m_type == IBridge::None)
        {
            try
            {
                // TODO: remove after tests
                LOG_DEBUG() << result.dump(4);
                txHash = result["result"].get<std::string>();
            }
            catch (const std::exception& ex)
            {
                error.m_type = IBridge::InvalidResultFormat;
                error.m_message = ex.what();
            }
        }

        callback(error, txHash);
    });
}

void EthereumBridge::send(
    const libbitcoin::short_hash& to,
    const libbitcoin::data_chunk& data,
    const ECC::uintBig& value,
    const ECC::uintBig& gas,
    const ECC::uintBig& gasPrice,
    std::function<void(const Error&, std::string, uint64_t)> callback)
{
    auto ethTx = std::make_shared<EthPendingTransaction>();
    ethTx->m_ethBaseTx.m_from = generateEthAddress();
    ethTx->m_ethBaseTx.m_receiveAddress = to;
    ethTx->m_ethBaseTx.m_value = value;
    ethTx->m_ethBaseTx.m_data = data;
    ethTx->m_ethBaseTx.m_gas = gas;
    ethTx->m_ethBaseTx.m_gasPrice = gasPrice;
    ethTx->m_confirmedCallback = callback;

    bool shouldProcessedNow = m_pendingTxs.empty();
    m_pendingTxs.push(ethTx);

    if (shouldProcessedNow)
    {
        processPendingTx();
    }
}

void EthereumBridge::processPendingTx()
{
    if (!m_pendingTxs.empty())
    {
        getTransactionCount([this, weak = this->weak_from_this()](const Error& error, uint64_t txCount)
        {
            if (!weak.expired())
            {
                onGotTransactionCount(error, txCount);
            }
        });
    }
}

void EthereumBridge::onGotTransactionCount(const Error& error, uint64_t txCount)
{
    auto ethTx = m_pendingTxs.front();
    Error tmp(error);

    if (tmp.m_type == IBridge::None)
    {
        try
        {
            ethTx->m_ethBaseTx.m_nonce = txCount;

            auto signedTx = ethTx->m_ethBaseTx.GetRawSigned(generatePrivateKey());
            if (signedTx.empty())
            {
                tmp.m_type = IBridge::EthError;
                tmp.m_message = "Failed to sign raw transaction!";
                ethTx->m_confirmedCallback(tmp, "", 0);
                return;
            }

            std::string stTx = libbitcoin::encode_base16(signedTx);

            sendRawTransaction(stTx, [this, txNonce = txCount, weak = this->weak_from_this()](const Error& error, const std::string& txHash)
            {
                if (!weak.expired())
                {
                    onSentRawTransaction(error, txHash, txNonce);
                }
            });

            return;
        }
        catch (const std::exception& ex)
        {
            tmp.m_type = IBridge::InvalidResultFormat;
            tmp.m_message = ex.what();
        }
    }

    ethTx->m_confirmedCallback(tmp, "", 0);
}

void EthereumBridge::onSentRawTransaction(const Error& error, const std::string& txHash, uint64_t txNonce)
{
    auto ethTx = m_pendingTxs.front();
    Error tmp(error);

    if (tmp.m_type == IBridge::None)
    {
        ethTx->m_txHash = txHash;
        requestTxConfirmation();

        // TODO(alex.starun): mb add additional callback
        // return txHash & txNonce
        ethTx->m_confirmedCallback(tmp, txHash, txNonce);
        return;
    }

    ethTx->m_confirmedCallback(tmp, "", 0);
}

void EthereumBridge::requestTxConfirmation()
{
    getTxByHash(m_pendingTxs.front()->m_txHash, [this, weak = this->weak_from_this()](const Error& error, const nlohmann::json& result)
    {
        if (!weak.expired())
        {
            onGotTxConfirmation(error, result);
        }
    });
}

void EthereumBridge::onGotTxConfirmation(const Error& error, const nlohmann::json& result)
{
    if (error.m_type == IBridge::None && !result.is_null())
    {
        // confirmed
        m_pendingTxs.pop();

        // process next
        processPendingTx();
        return;
    }

    // TODO(alex.starun): timer or asyncEvent
    constexpr unsigned kTxRequestIntervalMsec = 500;
    m_txConfirmationTimer->start(kTxRequestIntervalMsec, false, [this, weak = this->weak_from_this()]()
    {
        if (!weak.expired())
        {
            requestTxConfirmation();
        }
    });
}

void EthereumBridge::getTransactionReceipt(const std::string& txHash, std::function<void(const Error&, const nlohmann::json&)> callback)
{
    LOG_DEBUG() << "EthereumBridge::getTransactionReceipt";
    std::string params = (boost::format(R"("%1%")") % AddHexPrefix(txHash)).str();

    sendRequest("eth_getTransactionReceipt", params, [callback](Error error, const json& result)
    {
        LOG_DEBUG() << "EthereumBridge::getTransactionReceipt in";
        json txInfo;

        if (error.m_type == IBridge::None)
        {
            try
            {
                txInfo = result["result"];
            }
            catch (const std::exception& ex)
            {
                error.m_type = IBridge::InvalidResultFormat;
                error.m_message = ex.what();
            }
        }

        callback(error, txInfo);
    });
}

void EthereumBridge::getTxBlockNumber(const std::string& txHash, std::function<void(const Error&, uint64_t)> callback)
{
    LOG_DEBUG() << "EthereumBridge::getTxBlockNumber";
    getTransactionReceipt(txHash, [callback](const Error& error, const nlohmann::json& result)
    {
        LOG_DEBUG() << "EthereumBridge::getTxBlockNumber in";
        Error tmp(error);
        uint64_t txBlockNumber = 0;

        if (tmp.m_type == IBridge::None)
        {
            try
            {
                if (std::stoull(result["status"].get<std::string>(), nullptr, 16) == 0)
                {
                    tmp.m_type = IBridge::EthError;
                    tmp.m_message = "Status transaction is 0";
                }
                else
                {
                    txBlockNumber = std::stoull(result["blockNumber"].get<std::string>(), nullptr, 16);
                }
            }
            catch (const std::exception& ex)
            {
                tmp.m_type = IBridge::InvalidResultFormat;
                tmp.m_message = ex.what();
            }
        }

        callback(tmp, txBlockNumber);
    });
}

void EthereumBridge::getTxByHash(const std::string& txHash, std::function<void(const Error&, const nlohmann::json&)> callback)
{
    LOG_DEBUG() << "EthereumBridge::getTxByHash";
    std::string params = (boost::format(R"("%1%")") % AddHexPrefix(txHash)).str();

    sendRequest("eth_getTransactionByHash", params, [callback](Error error, const json& result)
    {
        LOG_DEBUG() << "EthereumBridge::getTxByHash in";
        json txInfo;

        if (error.m_type == IBridge::None)
        {
            try
            {
                txInfo = result["result"];
            }
            catch (const std::exception& ex)
            {
                error.m_type = IBridge::InvalidResultFormat;
                error.m_message = ex.what();
            }
        }

        callback(error, txInfo);
    });
}

void EthereumBridge::call(const libbitcoin::short_hash& to, const std::string& data, std::function<void(const Error&, const nlohmann::json&)> callback)
{
    LOG_DEBUG() << "EthereumBridge::call";
    std::string addr = ConvertEthAddressToStr(to);
    std::string params = (boost::format(R"({"to":"%1%","data":"%2%"},"latest")") % addr % AddHexPrefix(data)).str();

    sendRequest("eth_call", params, [callback](Error error, const json& result)
    {
        LOG_DEBUG() << "EthereumBridge::call in";
        json tmp;

        if (error.m_type == IBridge::None)
        {
            try
            {
                tmp = result["result"];
            }
            catch (const std::exception& ex)
            {
                error.m_type = IBridge::InvalidResultFormat;
                error.m_message = ex.what();
            }
        }

        callback(error, tmp);
    });
}

libbitcoin::short_hash EthereumBridge::generateEthAddress() const
{
    auto privateKey = generatePrivateKey();
    libbitcoin::ec_compressed point;

    libbitcoin::secret_to_public(point, privateKey);

    auto pk = libbitcoin::wallet::ec_public(point, false);
    auto rawPk = pk.encoded();

    return GetEthAddressFromPubkeyStr(rawPk);
}

void EthereumBridge::getGasPrice(std::function<void(const Error&, Amount)> callback)
{
    LOG_DEBUG() << "EthereumBridge::getGasPrice";
    sendRequest("eth_gasPrice", "", [callback](Error error, const json& result)
    {
        LOG_DEBUG() << "EthereumBridge::getGasPrice in";
        Amount gasPrice = 0;

        if (error.m_type == IBridge::None)
        {
            try
            {
                gasPrice = std::stoull(result["result"].get<std::string>(), nullptr, 16);
            }
            catch (const std::exception& ex)
            {
                error.m_type = IBridge::InvalidResultFormat;
                error.m_message = ex.what();
            }
        }

        callback(error, gasPrice);
    });
}

void EthereumBridge::sendRequest(
    const std::string& method, 
    const std::string& params, 
    std::function<void(const Error&, const nlohmann::json&)> callback)
{
    const std::string content = (boost::format(R"({"jsonrpc":"2.0","method":"%1%","params":[%2%], "id":1})") % method % params).str();
    auto settings = m_settingsProvider.GetSettings();
    std::string host;
    std::string path = "/";
    std::string url = settings.m_address;

    auto pos = url.find("://");

    // delete scheme
    if (pos != std::string::npos)
    {
        url.erase(0, pos + 3);
    }

    // get path
    pos = url.find("/");
    if (pos != std::string::npos)
    {
        path = url.substr(pos);
        url.erase(pos);
    }

    // get host
    pos = url.find(":");
    if (pos != std::string::npos)
    {
        host = url.substr(0, pos);
    }
    else
    {
        host = url;
        if (needSsl(settings.m_address))
        {
            url += ":443";
        }
    }

    io::Address address;

    LOG_DEBUG() << "sendRequest: " << content;

    if (!address.resolve(url.c_str()))
    {
        LOG_ERROR() << "unable to resolve electrum address: " << url;

        // TODO maybe to need async??
        Error error{ IOError, "unable to resolve ethereum provider address: " + url };
        json result;
        callback(error, result);
        return;
    }

    std::vector<HeaderPair> headers;

    headers.push_back({"Content-Type", "application/json"});
    headers.push_back({ "Host", host.c_str() });
    
    HttpClient::Request request;

    request.address(address)
        .connectTimeoutMsec(2000)
        .pathAndQuery(path.c_str())
        .headers(&headers.front())
        .numHeaders(headers.size())
        .method("POST")
        .body(content.c_str(), content.size());

    request.callback([callback](uint64_t id, const HttpMsgReader::Message& msg) -> bool
    {
        IBridge::Error error{ ErrorType::None, "" };
        json result;
        if (msg.what == HttpMsgReader::http_message)
        {
            size_t sz = 0;
            const void* body = msg.msg->get_body(sz);
            if (sz > 0 && body)
            {
                std::string strResponse = std::string(static_cast<const char*>(body), sz);

                LOG_DEBUG() << "strResponse = " << strResponse;

                try
                {
                    json reply = json::parse(strResponse);
                    if (!reply["error"].empty())
                    {
                        error.m_type = ErrorType::EthError;
                        error.m_message = reply["error"]["message"].get<std::string>();
                    }
                    else if (reply["result"].empty())
                    {
                        error.m_type = ErrorType::EmptyResult;
                        error.m_message = "JSON has no \"result\" value";
                    }
                    else
                    {
                        result = reply;
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
                error.m_type = ErrorType::InvalidResultFormat;
                error.m_message = "Empty response.";
            }
        }
        else
        {
            error.m_type = IBridge::ErrorType::IOError;
            error.m_message = msg.error_str();
        }
        callback(error, result);
        return false;
    });

    m_httpClient.send_request(request);
}

libbitcoin::ec_secret EthereumBridge::generatePrivateKey() const
{
    auto settings = m_settingsProvider.GetSettings();
    auto seed = libbitcoin::wallet::decode_mnemonic(settings.m_secretWords);
    libbitcoin::data_chunk seed_chunk(libbitcoin::to_chunk(seed));

    const auto prefixes = libbitcoin::wallet::hd_private::to_prefixes(0, 0);
    libbitcoin::wallet::hd_private private_key(seed_chunk, prefixes);

    private_key = ProcessHDPrivate(private_key, 44);
    private_key = ProcessHDPrivate(private_key, 60);
    private_key = ProcessHDPrivate(private_key, 0);
    private_key = ProcessHDPrivate(private_key, 0, false);
    private_key = ProcessHDPrivate(private_key, settings.m_accountIndex, false);

    return private_key.secret();
}
} // namespace beam::ethereum