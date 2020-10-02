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
#include "core/ecc.h"

using json = nlohmann::json;

namespace
{
libbitcoin::wallet::hd_private ProcessHDPrivate(const libbitcoin::wallet::hd_private& privateKey, uint32_t index, bool hard = true)
{
    static constexpr auto first = libbitcoin::wallet::hd_first_hardened_key;
    const auto position = hard ? first + index : index;
    return privateKey.derive_private(position);
}
}

namespace beam::ethereum
{
EthereumBridge::EthereumBridge(io::Reactor& reactor, ISettingsProvider& settingsProvider)
    : m_httpClient(reactor)
    , m_settingsProvider(settingsProvider)
{
}

void EthereumBridge::getBalance(std::function<void(ECC::uintBig)> callback)
{
    std::string ethAddress = ConvertEthAddressToStr(generateEthAddress());
    sendRequest("eth_getBalance", "\"" + ethAddress + "\",\"latest\"", [callback](const json& result) {
        std::string strBalance = result["result"].get<std::string>();
        strBalance.erase(0, 2);

        libbitcoin::data_chunk dc;
        libbitcoin::decode_base16(dc, strBalance);

        ECC::uintBig balance = ECC::Zero;
        std::copy(dc.crbegin(), dc.crend(), std::rbegin(balance.m_pData));

        //if (error.m_type == IBridge::None)
        //{
        //    try
        //    {
        //        // TODO should avoid using of double type
        //        balance = btc_to_satoshi(result.get<double>());
        //    }
        //    catch (const std::exception& ex)
        //    {
        //        error.m_type = IBridge::InvalidResultFormat;
        //        error.m_message = ex.what();
        //    }
        //}
        callback(balance);
    });
}

void EthereumBridge::getBlockNumber(std::function<void(Amount)> callback)
{
    sendRequest("eth_blockNumber", "", [callback](const json& result) {
        std::string strBlockNumber = result["result"].get<std::string>();
        Amount blockNumber = std::stoull(strBlockNumber, nullptr, 16);

        //if (error.m_type == IBridge::None)
        //{
        //    try
        //    {
        //        // TODO should avoid using of double type
        //        balance = btc_to_satoshi(result.get<double>());
        //    }
        //    catch (const std::exception& ex)
        //    {
        //        error.m_type = IBridge::InvalidResultFormat;
        //        error.m_message = ex.what();
        //    }
        //}
        callback(blockNumber);
    });
}

void EthereumBridge::getTransactionCount(std::function<void(Amount)> callback)
{
    std::string ethAddress = ConvertEthAddressToStr(generateEthAddress());
    sendRequest("eth_getTransactionCount", "\"" + ethAddress + "\",\"latest\"", [callback](const json& result) {
        std::string strBlockNumber = result["result"].get<std::string>();
        Amount blockNumber = std::stoull(strBlockNumber, nullptr, 16);

        callback(blockNumber);
    });
}

void EthereumBridge::sendRawTransaction(const std::string& rawTx, std::function<void(std::string)> callback)
{
    sendRequest("eth_sendRawTransaction", "\"" + rawTx + "\"", [callback](const json& result) {
        // TODO: remove after tests
        LOG_DEBUG() << result.dump(4);
        std::string txHash = result["result"].get<std::string>();

        callback(txHash);
    });
}

void EthereumBridge::getTransactionReceipt(const std::string& txHash, std::function<void()> callback)
{
    sendRequest("eth_getTransactionReceipt", "\"" + txHash + "\"", [callback](const json& result) {
        callback();
    });
}

void EthereumBridge::call(const libbitcoin::short_hash& to, const std::string& data, std::function<void()> callback)
{
    std::string addr = ConvertEthAddressToStr(to);
    sendRequest("eth_call", "{\"to\":\"" + addr + "\",\"data\":\"" + data + "\"},\"latest\"", [callback](const json& result) {
        
        callback();
    });
}

libbitcoin::short_hash EthereumBridge::generateEthAddress() const
{
    auto privateKey = generatePrivateKey();
    libbitcoin::ec_compressed point;

    libbitcoin::secret_to_public(point, privateKey);

    auto pk = libbitcoin::wallet::ec_public(point, false);
    auto rawPk = pk.encoded();

    auto tmp = beam::from_hex(std::string(rawPk.begin() + 2, rawPk.end()));

    auto hash = ethash::keccak256(&tmp[0], tmp.size());
    libbitcoin::short_hash address;
    libbitcoin::data_chunk data;

    std::copy_n(&hash.bytes[12], 20, address.begin());

    return address;
}

void EthereumBridge::sendRequest(const std::string& method, const std::string& params, std::function<void(const nlohmann::json&)> callback)
{
    const std::string content = R"({"jsonrpc":"2.0","method":")" + method + R"(","params":[)" + params + R"(], "id":1})";
    auto settings = m_settingsProvider.GetSettings();
    io::Address address;

    if (!address.resolve(settings.m_address.c_str()))
    {

        LOG_ERROR() << "unable to resolve electrum address: " << settings.m_address;

        // TODO maybe to need async??
        /*Error error{ IOError, "unable to resolve electrum address: " + electrumSettings.m_address };
        json result;
        callback(error, result, 0);*/
        return;
    }

    const HeaderPair headers[] = {
        {"Content-Type", "application/json"}
    };
    HttpClient::Request request;

    request.address(address)
        .connectTimeoutMsec(2000)
        .pathAndQuery("/")
        .headers(headers)
        .numHeaders(1)
        //.contentType("Content-Type: application/json")
        .method("POST")
        .body(content.c_str(), content.size());

    request.callback([callback](uint64_t id, const HttpMsgReader::Message& msg) -> bool
    {
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

                    callback(reply);
                    /*if (!reply["error"].empty())
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
                    }*/
                }
                catch (const std::exception& /*ex*/)
                {
                    /*error.m_type = IBridge::InvalidResultFormat;
                    error.m_message = ex.what();*/
                }
            }
            else
            {
                /*error.m_type = IBridge::ErrorType::InvalidResultFormat;
                error.m_message = "Empty response.";*/
            }
        }
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