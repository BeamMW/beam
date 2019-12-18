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
#include "common.h"

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
        const char kInvalidGenesisBlockHashMsg[] = "Invalid genesis block hash";

        class ScopedHttpRequest
        {
        public:
            const HttpClient::Request& request() const noexcept
            {
                return m_request;
            }
            void setId(uint64_t id)
            {
                m_request.id(id);
            }
            void setAddress(const io::Address& address)
            {
                m_request.address(address);
            }
            void setConnectTimeoutMsec(unsigned connectTimeoutMsec)
            {
                m_request.connectTimeoutMsec(connectTimeoutMsec);
            }
            void setPathAndQuery(const std::string& pathAndQuery)
            {
                m_pathAndQuery = pathAndQuery;
                m_request.pathAndQuery(m_pathAndQuery.c_str());
            }
            void addHeader(const std::string& key, const std::string& value)
            {
                const auto& header= m_headersData.emplace_back(key, value);
                m_headers.emplace_back(header.first.c_str(), header.second.c_str());

                m_request.headers(m_headers.data());
                m_request.numHeaders(m_headers.size());
            }
            void setMethod(const std::string& method)
            {
                m_method = method;
                m_request.method(m_method.data());
            }
            void setBody(const std::string& body)
            {
                m_body = body;
                m_request.body(m_body.c_str(), m_body.size());
            }
            void setCallback(const HttpClient::OnResponse& callback)
            {
                m_request.callback(callback);
            }
            void setContentType(const std::string& contentType)
            {
                m_contentType = contentType;
                m_request.contentType(m_contentType.c_str());
            }

        private:
            std::string m_method;
            std::string m_pathAndQuery;
            std::vector<std::pair<std::string, std::string>> m_headersData;
            std::vector<HeaderPair> m_headers;
            std::string m_body;
            std::string m_contentType;

            HttpClient::Request m_request;
        };

        std::pair<json, IBridge::Error> parseHttpResponse(const HttpMsgReader::Message& msg)
        {
            IBridge::Error error{ IBridge::ErrorType::None, "" };
            json result;

            if (msg.what == HttpMsgReader::http_message)
            {
                int httpStatus = msg.msg->get_status();
                if (httpStatus == HTTP_UNAUTHORIZED)
                {
                    error.m_type = IBridge::ErrorType::InvalidCredentials;
                    error.m_message = "Invalid credentials.";
                }
                else if (httpStatus >= HTTP_BAD_REQUEST && httpStatus != HTTP_BAD_REQUEST && httpStatus != HTTP_NOT_FOUND && httpStatus != HTTP_INTERNAL_SERVER_ERROR)
                {
                    error.m_type = IBridge::ErrorType::IOError;
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
                        catch (const std::exception & ex)
                        {
                            error.m_type = IBridge::InvalidResultFormat;
                            error.m_message = ex.what();
                        }
                    }
                    else
                    {
                        error.m_type = IBridge::ErrorType::InvalidResultFormat;
                        error.m_message = "Empty response.";
                    }
                }
            }
            else
            {
                error.m_type = IBridge::ErrorType::IOError;
                error.m_message = msg.error_str();
            }
            return { result, error };
        }
    }

    BitcoinCore016::BitcoinCore016(io::Reactor& reactor, ISettingsProvider& settingsProvider)
        : m_httpClient(reactor)
        , m_settingsProvider(settingsProvider)
    {
    }

    void BitcoinCore016::fundRawTransaction(const std::string& rawTx, Amount feeRate, std::function<void(const IBridge::Error&, const std::string&, int)> callback)
    {
        LOG_DEBUG() << "Send fundRawTransaction command";

        std::string params = "\"" + rawTx + "\"";
        if (feeRate)
        {
            params += ", {\"feeRate\": " + libbitcoin::satoshi_to_btc(feeRate) + "}";
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

        args += ",{\"" + withdrawAddress + "\": " + libbitcoin::satoshi_to_btc(amount) + "}";
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

    void BitcoinCore016::getTxOut(const std::string& txid, int outputIndex, std::function<void(const IBridge::Error&, const std::string&, Amount, uint32_t)> callback)
    {
        LOG_DEBUG() << "Send getTxOut command";

        sendRequest("gettxout", "\"" + txid + "\"" + "," + std::to_string(outputIndex), [callback](IBridge::Error error, const json& result) {
            Amount value = 0;
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
                    // TODO should avoid using of double type
                    value = btc_to_satoshi(result["value"].get<double>());
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
                    // TODO should avoid using of double type
                    balance = btc_to_satoshi(result.get<double>());
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
                    // TODO should avoid using of double type
                    confirmed = btc_to_satoshi(result["balance"].get<double>());
                    unconfirmed = btc_to_satoshi(result["unconfirmed_balance"].get<double>());
                    immature = btc_to_satoshi(result["immature_balance"].get<double>());
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

    void BitcoinCore016::getGenesisBlockHash(std::function<void(const Error&, const std::string&)> callback)
    {
        sendRequest("getblockhash", "0", [callback](IBridge::Error error, const json& result)
        {
            std::string genesisBlockHash;

            if (error.m_type == IBridge::None)
            {
                try
                {
                    genesisBlockHash = result.get<std::string>();
                }
                catch (const std::exception & ex)
                {
                    error.m_type = IBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }
            callback(error, genesisBlockHash);
        });
    }

    std::string BitcoinCore016::getCoinName() const
    {
        return "bitcoin";
    }

    void BitcoinCore016::sendRequest(const std::string& method, const std::string& params, std::function<void(const Error&, const json&)> callback)
    {
        const std::string content = R"({"method":")" + method + R"(","params":[)" + params + "]}";
        auto settings = m_settingsProvider.GetSettings();
        auto connectionSettings = settings.GetConnectionOptions();
        const std::string authorization = connectionSettings.generateAuthorization();
        auto request = std::make_shared<ScopedHttpRequest>();

        request->setAddress(connectionSettings.m_address);
        request->setConnectTimeoutMsec(2000);
        request->setPathAndQuery("/");
        request->addHeader("Authorization", authorization);
        request->setMethod("POST");
        request->setBody(content);

        request->setCallback([coinName = getCoinName(), callback](uint64_t id, const HttpMsgReader::Message& msg) -> bool {
            const auto [result, error] = parseHttpResponse(msg);
            callback(error, result);
            return false;
        });

        // find address in map
        auto iter = m_verifiedAddresses.find(connectionSettings.m_address);
        if (iter != m_verifiedAddresses.end())
        {
            // node have invalid genesis block hash
            if (!iter->second)
            {
                // error
                Error error{ InvalidGenesisBlock, kInvalidGenesisBlockHashMsg };
                json result;
                callback(error, result);
                return;
            }
            // node already verified
            m_httpClient.send_request(request->request());
        }
        else
        {
            // Node have not validated yet
            HttpClient::Request verificationRequest;
            const std::string verificationContent = R"({"method":"getblockhash","params":[0], "id": "verify"})";
            const HeaderPair headers[] = {
                {"Authorization", authorization.c_str()}
            };

            verificationRequest.address(connectionSettings.m_address)
                .connectTimeoutMsec(2000)
                .pathAndQuery("/")
                .headers(headers)
                .numHeaders(1)
                .method("POST")
                .body(verificationContent.c_str(), verificationContent.size());

            verificationRequest.callback([coinName = getCoinName(), callback, request, this, settings](uint64_t id, const HttpMsgReader::Message& msg) -> bool {
                auto [result, error] = parseHttpResponse(msg);

                if (error.m_type == None)
                {
                    try
                    {
                        auto genesisBlockHash = result.get<std::string>();
                        auto genesisBlockHashes = settings.GetGenesisBlockHashes();
                        auto currentNodeAddress = settings.GetConnectionOptions().m_address;

                        if (std::find(genesisBlockHashes.begin(), genesisBlockHashes.end(), genesisBlockHash) != genesisBlockHashes.end())
                        {
                            m_verifiedAddresses.emplace(currentNodeAddress, true);
                            m_httpClient.send_request(request->request());
                            return false;
                        }
                        else
                        {
                            m_verifiedAddresses.emplace(currentNodeAddress, false);

                            error.m_message = kInvalidGenesisBlockHashMsg;
                            error.m_type = IBridge::InvalidGenesisBlock;
                        }
                    }
                    catch (const std::exception & ex)
                    {
                        error.m_type = IBridge::InvalidResultFormat;
                        error.m_message = ex.what();
                    }
                }
                callback(error, result);
                return false;
            });
            m_httpClient.send_request(verificationRequest);
        }
    }
} // namespace beam::bitcoin