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

#include <bitcoin/bitcoin.hpp>

using json = nlohmann::json;

namespace
{
bool needSsl(const std::string& address)
{
    // TODO roman.strilets need insensitive
    return address.find("infura") != std::string::npos;
}
}

namespace beam::ethereum
{
EthereumBridge::EthereumBridge(io::Reactor& reactor, ISettingsProvider& settingsProvider)
    : m_httpClient(reactor, settingsProvider.GetSettings().NeedSsl())
    , m_settingsProvider(settingsProvider)
    , m_txConfirmationTimer(io::Timer::create(reactor))
    , m_approveTxConfirmationTimer(io::Timer::create(reactor))
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
    ethTx->m_callback = callback;

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
            }
            else
            {
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
        }
        catch (const std::exception& ex)
        {
            tmp.m_type = IBridge::InvalidResultFormat;
            tmp.m_message = ex.what();
        }
    }

    ethTx->m_callback(tmp, "", 0);

    m_pendingTxs.pop();
    processPendingTx();
}

void EthereumBridge::onSentRawTransaction(const Error& error, const std::string& txHash, uint64_t txNonce)
{
    auto ethTx = m_pendingTxs.front();
    Error tmp(error);

    if (tmp.m_type == IBridge::None)
    {
        ethTx->m_txHash = txHash;
        requestTxConfirmation();

        // return txHash & txNonce
        ethTx->m_callback(tmp, txHash, txNonce);
        return;
    }

    ethTx->m_callback(tmp, "", 0);
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
        // TODO(alex.starun): "confirmed" callback?
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

void EthereumBridge::erc20Approve(
    const libbitcoin::short_hash& token,
    const libbitcoin::short_hash& spender,
    const ECC::uintBig& value,
    const ECC::uintBig& gas,
    const ECC::uintBig& gasPrice,
    ERC20ApproveCallback&& callback)
{
    auto pendingApproval = std::make_shared<EthPendingApprove>(token, spender, gas, gasPrice, value, std::move(callback));

    m_pendingApprovals.push(pendingApproval);

    if (m_pendingApprovals.size() == 1)
    {
        processPendingApproveTx();
    }
}

void EthereumBridge::processPendingApproveTx()
{
    if (!m_pendingApprovals.empty())
    {
        auto pendingTx = m_pendingApprovals.front();

        getTokenBalance(ConvertEthAddressToStr(pendingTx->m_token), [this, weak = this->weak_from_this()](const Error& error, const std::string& result)
        {
            if (!weak.expired())
            {
                onGotTokenBalance(error, result);
            }
        });
    }
}

void EthereumBridge::onGotTokenBalance(const Error& error, const std::string& result)
{
    auto pendingTx = m_pendingApprovals.front();

    if (error.m_type == IBridge::None)
    {
        pendingTx->m_tokenBalance = ConvertStrToUintBig(result);

        libbitcoin::data_chunk data;
        data.reserve(ethereum::kEthContractMethodHashSize + 2 * ethereum::kEthContractABIWordSize);
        libbitcoin::decode_base16(data, ethereum::ERC20Hashes::kAllowanceHash);
        ethereum::AddContractABIWordToBuffer(generateEthAddress(), data);
        ethereum::AddContractABIWordToBuffer(pendingTx->m_spender, data);

        call(pendingTx->m_token, libbitcoin::encode_base16(data), [this, weak = this->weak_from_this()](const Error& error, const nlohmann::json& result)
        {
            if (!weak.expired())
            {
                onGotAllowance(error, result);
            }
        });
        return;
    }

    // error
    pendingTx->m_callback(error, "");

    // process next
    m_pendingApprovals.pop();
    processPendingApproveTx();
}

void EthereumBridge::onGotAllowance(const Error& error, const nlohmann::json& result)
{
    auto pendingTx = m_pendingApprovals.front();
    Error tmp(error);

    if (tmp.m_type == IBridge::None)
    {
        try
        {
            auto allowance = ConvertStrToUintBig(result.get<std::string>());

            if (allowance < pendingTx->m_tokenBalance)
            {
                // reset allowance
                // ERC20::approve + spender + 0
                libbitcoin::data_chunk data;
                data.reserve(ethereum::kEthContractMethodHashSize + 2 * ethereum::kEthContractABIWordSize);
                libbitcoin::decode_base16(data, ethereum::ERC20Hashes::kApproveHash);
                ethereum::AddContractABIWordToBuffer(pendingTx->m_spender, data);
                data.insert(data.end(), ethereum::kEthContractABIWordSize, 0x00);

                send(pendingTx->m_token, data, ECC::Zero, pendingTx->m_gas, pendingTx->m_gasPrice,
                    [this, weak = this->weak_from_this()](const ethereum::IBridge::Error& error, const std::string& txHash, uint64_t txNonce)
                {
                    if (!weak.expired())
                    {
                        onResetAllowance(error, txHash, txNonce);
                    }
                });

                return;
            }
        }
        catch (const std::exception& ex)
        {
            tmp.m_type = IBridge::InvalidResultFormat;
            tmp.m_message = ex.what();
        }
    }

    pendingTx->m_callback(tmp, "");

    // process next
    m_pendingApprovals.pop();
    processPendingApproveTx();
}

void EthereumBridge::onResetAllowance(const ethereum::IBridge::Error& error, const std::string&, uint64_t)
{
    auto pendingTx = m_pendingApprovals.front();
    Error tmp(error);

    if (tmp.m_type == IBridge::None)
    {
        try
        {
            // ERC20::approve + spender + value
            libbitcoin::data_chunk data;
            data.reserve(ethereum::kEthContractMethodHashSize + 2 * ethereum::kEthContractABIWordSize);
            libbitcoin::decode_base16(data, ethereum::ERC20Hashes::kApproveHash);
            ethereum::AddContractABIWordToBuffer(pendingTx->m_spender, data);
            ethereum::AddContractABIWordToBuffer({ std::begin(pendingTx->m_tokenBalance.m_pData), std::end(pendingTx->m_tokenBalance.m_pData) }, data);

            send(pendingTx->m_token, data, ECC::Zero, pendingTx->m_gas, pendingTx->m_gasPrice,
                [this, weak = this->weak_from_this()](const ethereum::IBridge::Error& error, const std::string& txHash, uint64_t txNonce)
            {
                if (!weak.expired())
                {
                    onSentApprove(error, txHash, txNonce);
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

    // error
    pendingTx->m_callback(tmp, "");

    // process next
    m_pendingApprovals.pop();
    processPendingApproveTx();
}

void EthereumBridge::onSentApprove(const ethereum::IBridge::Error& error, const std::string& txHash, uint64_t txNonce)
{
    if (error.m_type == IBridge::None)
    {
        m_pendingApprovals.front()->m_txHash = txHash;
        requestApproveTxConfirmation();
        return;
    }

    // error
    m_pendingApprovals.front()->m_callback(error, "");

    // process next
    m_pendingApprovals.pop();
    processPendingApproveTx();
}

void EthereumBridge::requestApproveTxConfirmation()
{
    auto txHash = m_pendingApprovals.front()->m_txHash;
    getTxBlockNumber(txHash, [this, weak = this->weak_from_this()](const ethereum::IBridge::Error& error, uint64_t txBlockNumber)
    {
        if (!weak.expired())
        {
            onGotApproveTxConfirmation(error, txBlockNumber);
        }
    });
}

void EthereumBridge::onGotApproveTxConfirmation(const Error& error, uint64_t txBlockNumber)
{
    if (error.m_type == IBridge::EmptyResult)
    {
        constexpr unsigned kApproveTxRequestIntervalMsec = 1000;
        m_approveTxConfirmationTimer->start(kApproveTxRequestIntervalMsec, false, [this, weak = this->weak_from_this()]()
        {
            if (!weak.expired())
            {
                requestApproveTxConfirmation();
            }
        });
        return;
    }

    // callback
    auto pendingTx = m_pendingApprovals.front();
    pendingTx->m_callback(error, pendingTx->m_txHash);

    // process next
    m_pendingApprovals.pop();
    processPendingApproveTx();
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
    auto settings = m_settingsProvider.GetSettings();
    return GenerateEthereumAddress(settings.m_secretWords, settings.m_accountIndex);
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
    std::string url = settings.GetEthNodeAddress();

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

    std::string host = settings.GetEthNodeHost();
    std::vector<HeaderPair> headers;
    headers.push_back({"Content-Type", "application/json"});
    headers.push_back({ "Host", host.c_str() });
    
    HttpClient::Request request;
    std::string path = settings.GetPathAndQuery();

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

    return GeneratePrivateKey(settings.m_secretWords, settings.m_accountIndex);
}
} // namespace beam::ethereum