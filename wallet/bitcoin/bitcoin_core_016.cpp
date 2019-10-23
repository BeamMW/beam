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

#include "bitcoin_core_016.h"

#include "bitcoin/bitcoin.hpp"
#include "nlohmann/json.hpp"
#include "utility/logger.h"

using json = nlohmann::json;

namespace beam::bitcoin
{    
    namespace
    {
        enum HTTPStatusCode : int
        {
            HTTP_OK = 200,
            HTTP_BAD_REQUEST = 400,
            HTTP_UNAUTHORIZED = 401,
            HTTP_FORBIDDEN = 403,
            HTTP_NOT_FOUND = 404,
            HTTP_BAD_METHOD = 405,
            HTTP_INTERNAL_SERVER_ERROR = 500,
            HTTP_SERVICE_UNAVAILABLE = 503,
        };
    }

    BitcoinCore016::BitcoinCore016(io::Reactor& reactor, IBitcoinCoreSettingsProvider& settingsProvider)
        : m_httpClient(reactor)
        , m_settingsProvider(settingsProvider)
    {
    }

    void BitcoinCore016::dumpPrivKey(const std::string& btcAddress, std::function<void(const IBridge::Error&, const std::string&)> callback)
    {
        LOG_DEBUG() << "Send dumpPrivKey command";

        sendRequest("dumpprivkey", "\"" + btcAddress + "\"", [callback] (IBridge::Error error, const json& result){
            std::string privKey;

            if (error.m_type == IBridge::None)
            {
                try
                {
                    privKey = result.get<std::string>();
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }

            callback(error, privKey);
        });
    }

    void BitcoinCore016::fundRawTransaction(const std::string& rawTx, Amount feeRate, std::function<void(const IBridge::Error&, const std::string&, int)> callback)
    {
        LOG_DEBUG() << "Send fundRawTransaction command";

        std::string params = "\"" + rawTx + "\"";
        if (feeRate)
        {
            params += ", {\"feeRate\": " + std::to_string(double(feeRate) / libbitcoin::satoshi_per_bitcoin) + "}";
        }

        sendRequest("fundrawtransaction", params, [callback](IBridge::Error error, const json& result) {
            std::string hex;
            int changepos = -1;

            if (error.m_type == IBridge::None)
            {
                try
                {
                    hex = result["hex"].get<std::string>();
                    changepos = result["changepos"].get<int>();
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }

            callback(error, hex, changepos);
        });
    }

    void BitcoinCore016::signRawTransaction(const std::string& rawTx, std::function<void(const IBridge::Error&, const std::string&, bool)> callback)
    {
        LOG_DEBUG() << "Send signRawTransaction command";

        sendRequest("signrawtransaction", "\"" + rawTx + "\"", [callback](IBridge::Error error, const json& result) {
            std::string hex;
            bool isComplete = false;

            if (error.m_type == IBridge::None)
            {
                try
                {
                    hex = result["hex"].get<std::string>();
                    isComplete = result["complete"].get<bool>();
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }

            callback(error, hex, isComplete);
        });
    }

    void BitcoinCore016::sendRawTransaction(const std::string& rawTx, std::function<void(const IBridge::Error&, const std::string&)> callback)
    {
        LOG_DEBUG() << "Send sendRawTransaction command";

        sendRequest("sendrawtransaction", "\"" + rawTx + "\"", [callback](IBridge::Error error, const json& result) {
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
        });
    }

    void BitcoinCore016::getRawChangeAddress(std::function<void(const IBridge::Error&, const std::string&)> callback)
    {
        LOG_DEBUG() << "Send getRawChangeAddress command";

        sendRequest("getrawchangeaddress", "\"legacy\"", [callback](IBridge::Error error, const json& result) {
            std::string address;

            if (error.m_type == IBridge::None)
            {
                try
                {
                    address = result.get<std::string>();
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }

            callback(error, address);
        });
    }

    void BitcoinCore016::createRawTransaction(
        const std::string& withdrawAddress,
        const std::string& contractTxId,
        Amount amount,
        int outputIndex,
        Timestamp locktime,
        std::function<void(const IBridge::Error&, const std::string&)> callback)
    {
        LOG_DEBUG() << "Send createRawTransaction command";

        std::string args("[{\"txid\": \"" + contractTxId + "\", \"vout\":" + std::to_string(outputIndex) + ", \"Sequence\": " + std::to_string(libbitcoin::max_input_sequence - 1) + " }]");

        args += ",{\"" + withdrawAddress + "\": " + std::to_string(double(amount) / libbitcoin::satoshi_per_bitcoin) + "}";
        if (locktime)
        {
            args += "," + std::to_string(locktime);
        }
        sendRequest("createrawtransaction", args, [callback](IBridge::Error error, const json& result) {
            std::string tx;

            if (error.m_type == IBridge::None)
            {
                try
                {
                    tx = result.get<std::string>();

                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }

            callback(error, tx);
        });
    }

    void BitcoinCore016::getTxOut(const std::string& txid, int outputIndex, std::function<void(const IBridge::Error&, const std::string&, double, uint32_t)> callback)
    {
        LOG_DEBUG() << "Send getTxOut command";

        sendRequest("gettxout", "\"" + txid + "\"" + "," + std::to_string(outputIndex), [callback](IBridge::Error error, const json& result) {
            double value = 0;
            uint16_t confirmations = 0;
            std::string scriptHex;

            if (error.m_type == IBridge::EmptyResult)
            {
                // it's normal case for getTxOut
                error.m_type = IBridge::None;
                error.m_message = "";
            }
            else if (error.m_type == IBridge::None)
            {
                try
                {
                    scriptHex = result["scriptPubKey"]["hex"].get<std::string>();
                    value = result["value"].get<double>();
                    confirmations = result["confirmations"];
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }

            callback(error, scriptHex, value, confirmations);
        });
    }

    void BitcoinCore016::getBlockCount(std::function<void(const IBridge::Error&, uint64_t)> callback)
    {
        LOG_DEBUG() << "Send getBlockCount command";

        sendRequest("getblockcount", "", [callback](IBridge::Error error, const json& result) {
            uint64_t blockCount = 0;

            if (error.m_type == IBridge::EmptyResult)
            {
                // it's normal case for getBlockCount
                error.m_type = IBridge::None;
                error.m_message = "";
            }
            else if (error.m_type == IBridge::None)
            {
                try
                {
                    blockCount = result.get<uint64_t>();
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }
            callback(error, blockCount);
        });
    }

    void BitcoinCore016::getBalance(uint32_t confirmations, std::function<void(const Error&, Amount)> callback)
    {
        LOG_DEBUG() << "Send getBalance command";
        sendRequest("getbalance", "\"*\"," + std::to_string(confirmations), [callback](IBridge::Error error, const json& result) {
            Amount balance = 0;

            if (error.m_type == IBridge::None)
            {
                try
                {
                    // TODO should be avoided to use conversion to double and vice versa
                    libbitcoin::btc_to_satoshi(balance, result.dump());
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

    void BitcoinCore016::getDetailedBalance(std::function<void(const Error&, Amount, Amount, Amount)> callback)
    {
        //LOG_DEBUG() << "Send getWalletInfo command";
        sendRequest("getwalletinfo", "", [callback](IBridge::Error error, const json& result) {
            Amount confirmed = 0;
            Amount unconfirmed = 0;
            Amount immature = 0;

            if (error.m_type == IBridge::None)
            {
                try
                {
                    // TODO should be avoided to use conversion to double and vice versa
                    libbitcoin::btc_to_satoshi(confirmed, result["balance"].dump());
                    libbitcoin::btc_to_satoshi(unconfirmed, result["unconfirmed_balance"].dump());
                    libbitcoin::btc_to_satoshi(immature, result["immature_balance"].dump());
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }
            callback(error, confirmed, unconfirmed, immature);
            });
    }

    std::string BitcoinCore016::getCoinName() const
    {
        return "bitcoin";
    }

    void BitcoinCore016::sendRequest(const std::string& method, const std::string& params, std::function<void(const Error&, const json&)> callback)
    {
        const std::string content(R"({"method":")" + method + R"(","params":[)" + params + "]}");
        auto settings = m_settingsProvider.GetBitcoinCoreSettings();
        const std::string authorization(settings.generateAuthorization());
        const HeaderPair headers[] = {
            {"Authorization", authorization.data()}
        };
        HttpClient::Request request;

        request.address(settings.m_address)
            .connectTimeoutMsec(2000)
            .pathAndQuery("/")
            .headers(headers)
            .numHeaders(1)
            .method("POST")
            .body(content.c_str(), content.size());

        request.callback([coinName = getCoinName(), callback](uint64_t id, const HttpMsgReader::Message& msg) -> bool {
            Error error{ None, "" };
            json result;

            if (msg.what == HttpMsgReader::http_message)
            {
                int httpStatus = msg.msg->get_status();
                if (httpStatus == HTTP_UNAUTHORIZED)
                {
                    error.m_type = InvalidCredentials;
                    error.m_message = "Invalid credentials.";
                }
                else if (httpStatus >= HTTP_BAD_REQUEST && httpStatus != HTTP_BAD_REQUEST && httpStatus != HTTP_NOT_FOUND && httpStatus != HTTP_INTERNAL_SERVER_ERROR)
                {
                    error.m_type = IOError;
                    error.m_message = "HTTP status: " + std::to_string(httpStatus);
                }
                else
                {
                    size_t sz = 0;
                    const void* body = msg.msg->get_body(sz);
                    if (sz > 0 && body)
                    {
                        std::string strResponse = std::string(static_cast<const char*>(body), sz);

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
                }
            }
            else
            {
                error.m_type = IOError;
                error.m_message = msg.error_str();
            }

            callback(error, result);
            return false;
        });

        m_httpClient.send_request(request);
    }
} // namespace beam::bitcoin