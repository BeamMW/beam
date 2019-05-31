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

#include "bitcoind016.h"

#include "bitcoin/bitcoin.hpp"
#include "nlohmann/json.hpp"
#include "utility/logger.h"

using json = nlohmann::json;

namespace beam
{    
    namespace
    {
        std::string generateAuthorization(const std::string& userName, const std::string& pass)
        {
            std::string userWithPass(userName + ":" + pass);
            libbitcoin::data_chunk t(userWithPass.begin(), userWithPass.end());
            return std::string("Basic " + libbitcoin::encode_base64(t));
        }
    }

    Bitcoind016::Bitcoind016(io::Reactor& reactor, const BitcoinOptions& options)
        : m_httpClient(reactor)
        , m_options(options)
        , m_authorization(generateAuthorization(options.m_userName, options.m_pass))
    {
    }

    void Bitcoind016::dumpPrivKey(const std::string& btcAddress, std::function<void(const IBitcoinBridge::Error&, const std::string&)> callback)
    {
        LOG_DEBUG() << "Send to Bitcoind dumpPrivKey command";

        sendRequest("dumpprivkey", "\"" + btcAddress + "\"", [callback] (IBitcoinBridge::Error error, const std::string& response){
            std::string result;

            if (error.m_type == IBitcoinBridge::None)
            {
                try
                {
                    json reply = json::parse(response);

                    if (reply["error"].empty())
                    {
                        result = reply["result"].get<std::string>();
                    }
                    else
                    {
                        error.m_type = IBitcoinBridge::BitcoinError;
                        error.m_message = reply["error"]["message"].get<std::string>();
                    }
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBitcoinBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }

            callback(error, result);
        });
    }

    void Bitcoind016::fundRawTransaction(const std::string& rawTx, Amount feeRate, std::function<void(const IBitcoinBridge::Error&, const std::string&, int)> callback)
    {
        LOG_DEBUG() << "Send to Bitcoind fundRawTransaction command";

        std::string params = "\"" + rawTx + "\"";
        if (feeRate)
        {
            params += ", {\"feeRate\": " + std::to_string(double(feeRate) / libbitcoin::satoshi_per_bitcoin) + "}";
        }

        sendRequest("fundrawtransaction", params, [callback](IBitcoinBridge::Error error, const std::string& response) {
            std::string hex;
            int changepos = -1;

            if (error.m_type == IBitcoinBridge::None)
            {
                try
                {
                    json reply = json::parse(response);

                    if (reply["error"].empty())
                    {
                        const auto& result = reply["result"];

                        hex = result["hex"].get<std::string>();
                        changepos = result["changepos"].get<int>();
                    }
                    else
                    {
                        error.m_type = IBitcoinBridge::BitcoinError;
                        error.m_message = reply["error"]["message"].get<std::string>();
                    }
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBitcoinBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }

            callback(error, hex, changepos);
        });
    }

    void Bitcoind016::signRawTransaction(const std::string& rawTx, std::function<void(const IBitcoinBridge::Error&, const std::string&, bool)> callback)
    {
        LOG_DEBUG() << "Send to Bitcoind signRawTransaction command";

        sendRequest("signrawtransaction", "\"" + rawTx + "\"", [callback](IBitcoinBridge::Error error, const std::string& response) {
            std::string hex;
            bool isComplete = false;

            if (error.m_type == IBitcoinBridge::None)
            {
                try
                {
                    json reply = json::parse(response);

                    if (reply["error"].empty())
                    {
                        const auto& result = reply["result"];

                        hex = result["hex"].get<std::string>();
                        isComplete = result["complete"].get<bool>();
                    }
                    else
                    {
                        error.m_type = IBitcoinBridge::BitcoinError;
                        error.m_message = reply["error"]["message"].get<std::string>();
                    }
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBitcoinBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }

            callback(error, hex, isComplete);
        });
    }

    void Bitcoind016::sendRawTransaction(const std::string& rawTx, std::function<void(const IBitcoinBridge::Error&, const std::string&)> callback)
    {
        LOG_DEBUG() << "Send to Bitcoind sendRawTransaction command";

        sendRequest("sendrawtransaction", "\"" + rawTx + "\"", [callback](IBitcoinBridge::Error error, const std::string& response) {
            std::string result;

            if (error.m_type == IBitcoinBridge::None)
            {
                try
                {
                    json reply = json::parse(response);

                    if (reply["error"].empty())
                    {
                        result = reply["result"].get<std::string>();
                    }
                    else
                    {
                        error.m_type = IBitcoinBridge::BitcoinError;
                        error.m_message = reply["error"]["message"].get<std::string>();
                    }
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBitcoinBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }

            callback(error, result);
        });
    }

    void Bitcoind016::getRawChangeAddress(std::function<void(const IBitcoinBridge::Error&, const std::string&)> callback)
    {
        LOG_DEBUG() << "Send to Bitcoind getRawChangeAddress command";

        sendRequest("getrawchangeaddress", "\"legacy\"", [callback](IBitcoinBridge::Error error, const std::string& response) {
            std::string result;

            if (error.m_type == IBitcoinBridge::None)
            {
                try
                {
                    json reply = json::parse(response);

                    if (reply["error"].empty())
                    {
                        result = reply["result"].get<std::string>();
                    }
                    else
                    {
                        error.m_type = IBitcoinBridge::BitcoinError;
                        error.m_message = reply["error"]["message"].get<std::string>();
                    }
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBitcoinBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }

            callback(error, result);
        });
    }

    void Bitcoind016::createRawTransaction(
        const std::string& withdrawAddress,
        const std::string& contractTxId,
        uint64_t amount,
        int outputIndex,
        Timestamp locktime,
        std::function<void(const IBitcoinBridge::Error&, const std::string&)> callback)
    {
        LOG_DEBUG() << "Send to Bitcoind createRawTransaction command";

        std::string args("[{\"txid\": \"" + contractTxId + "\", \"vout\":" + std::to_string(outputIndex) + ", \"Sequence\": " + std::to_string(libbitcoin::max_input_sequence - 1) + " }]");

        args += ",{\"" + withdrawAddress + "\": " + std::to_string(double(amount) / libbitcoin::satoshi_per_bitcoin) + "}";
        if (locktime)
        {
            args += "," + std::to_string(locktime);
        }
        sendRequest("createrawtransaction", args, [callback](IBitcoinBridge::Error error, const std::string& response) {
            std::string result;

            if (error.m_type == IBitcoinBridge::None)
            {
                try
                {
                    json reply = json::parse(response);

                    if (reply["error"].empty())
                    {
                        result = reply["result"].empty() ? "" : reply["result"].get<std::string>();
                    }
                    else
                    {
                        error.m_type = IBitcoinBridge::BitcoinError;
                        error.m_message = reply["error"]["message"].get<std::string>();
                    }
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBitcoinBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }

            callback(error, result);
        });
    }

    void Bitcoind016::getTxOut(const std::string& txid, int outputIndex, std::function<void(const IBitcoinBridge::Error&, const std::string&, double, uint16_t)> callback)
    {
        LOG_DEBUG() << "Send to Bitcoind getTxOut command";

        sendRequest("gettxout", "\"" + txid + "\"" + "," + std::to_string(outputIndex), [callback](IBitcoinBridge::Error error, const std::string& response) {
            double value = 0;
            uint16_t confirmations = 0;
            std::string scriptHex;

            if (error.m_type == IBitcoinBridge::None)
            {
                try
                {
                    json reply = json::parse(response);

                    if (reply["error"].empty())
                    {
                        if (!reply["result"].empty())
                        {
                            const auto& result = reply["result"];
                            scriptHex = result["scriptPubKey"]["hex"].get<std::string>();
                            value = result["value"].get<double>();
                            confirmations = result["confirmations"];
                        }
                    }
                    else
                    {
                        error.m_type = IBitcoinBridge::BitcoinError;
                        error.m_message = reply["error"]["message"].get<std::string>();
                    }
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBitcoinBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }

            callback(error, scriptHex, value, confirmations);
        });
    }

    void Bitcoind016::getBlockCount(std::function<void(const IBitcoinBridge::Error&, uint64_t)> callback)
    {
        LOG_DEBUG() << "Send to Bitcoind getBlockCount command";

        sendRequest("getblockcount", "", [callback](IBitcoinBridge::Error error, const std::string& response) {
            uint64_t blockCount = 0;

            if (error.m_type == IBitcoinBridge::None)
            {
                try
                {
                    json reply = json::parse(response);

                    if (reply["error"].empty())
                    {
                        blockCount = reply["result"].empty() ? 0 : reply["result"].get<uint64_t>();
                    }
                    else
                    {
                        error.m_type = IBitcoinBridge::BitcoinError;
                        error.m_message = reply["error"]["message"].get<std::string>();
                    }
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBitcoinBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }
            callback(error, blockCount);
        });
    }

    void Bitcoind016::getBalance(uint32_t confirmations, std::function<void(const Error&, double)> callback)
    {
        LOG_DEBUG() << "Send to Bitcoind getBlockCount command";
        sendRequest("getbalance", "\"*\"," + std::to_string(confirmations), [callback](IBitcoinBridge::Error error, const std::string& response) {
            double balance = 0;

            if (error.m_type == IBitcoinBridge::None)
            {
                try
                {
                    json reply = json::parse(response);

                    if (reply["error"].empty())
                    {
                        balance = reply["result"].empty() ? 0 : reply["result"].get<double>();
                    }
                    else
                    {
                        error.m_type = IBitcoinBridge::BitcoinError;
                        error.m_message = reply["error"]["message"].get<std::string>();
                    }
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBitcoinBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }
            callback(error, balance);
        });
    }

    uint8_t Bitcoind016::getAddressVersion()
    {
        if (isMainnet())
        {
            return libbitcoin::wallet::ec_private::mainnet_wif;
        }
        
        return libbitcoin::wallet::ec_private::testnet_p2kh;
    }

    Amount Bitcoind016::getFeeRate() const
    {
        return m_options.m_feeRate;
    }

    uint16_t Bitcoind016::getTxMinConfirmations() const
    {
        return m_options.m_confirmations;
    }

    uint32_t Bitcoind016::getLockTimeInBlocks() const
    {
        return m_options.m_lockTimeInBlocks;
    }

    void Bitcoind016::sendRequest(const std::string& method, const std::string& params, std::function<void(const Error&, const std::string&)> callback)
    {
        const std::string content(R"({"method":")" + method + R"(","params":[)" + params + "]}");
        const HeaderPair headers[] = {
            {"Authorization", m_authorization.data()}
        };
        HttpClient::Request request;

        request.address(m_options.m_address)
            .connectTimeoutMsec(2000)
            .pathAndQuery("/")
            .headers(headers)
            .numHeaders(1)
            .method("POST")
            .body(content.c_str(), content.size());

        request.callback([callback](uint64_t id, const HttpMsgReader::Message& msg) -> bool {
            Error error{ None, "" };
            std::string response;

            if (msg.what == HttpMsgReader::http_message)
            {
                size_t sz = 0;
                const void* body = msg.msg->get_body(sz);
                if (sz > 0 && body)
                {
                    response = std::string(static_cast<const char*>(body), sz);
                    LOG_DEBUG() << "Bitcoin response: " << response;
                }
                else
                {
                    error.m_type = InvalidResultFormat;
                    error.m_message = "Empty response. Maybe wrong credentials.";
                }
            }
            else
            {
                error.m_type = IOError;
                error.m_message = msg.error_str();
            }

            callback(error, response);
            return false;
        });

        m_httpClient.send_request(request);
    }

    bool Bitcoind016::isMainnet() const
    {
        return m_options.m_mainnet;
    }
}