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

    Bitcoind016::Bitcoind016(io::Reactor& reactor, const std::string& userName, const std::string& pass,
        const io::Address& address, Amount feeRate, Amount confirmations, bool mainnet)
        : m_httpClient(reactor)
        , m_address(address)
        , m_authorization(generateAuthorization(userName, pass))
        , m_isMainnet(mainnet)
        , m_feeRate(feeRate)
        , m_confirmations(confirmations)
    {
    }

    void Bitcoind016::dumpPrivKey(const std::string& btcAddress, std::function<void(const std::string&, const std::string&)> callback)
    {
        sendRequest("dumpprivkey", "\"" + btcAddress + "\"", [callback] (const std::string& response){
            json reply = json::parse(response);
            std::string error = reply["error"].empty() ? "" : reply["error"]["message"].get<std::string>();
            std::string result = reply["result"].empty() ? "" : reply["result"].get<std::string>();

            callback(error, result);
        });
    }

    void Bitcoind016::fundRawTransaction(const std::string& rawTx, Amount feeRate, std::function<void(const std::string&, const std::string&, int)> callback)
    {
        std::string params = "\"" + rawTx + "\"";
        if (feeRate)
        {
            params += ", {\"feeRate\": " + std::to_string(double(feeRate) / libbitcoin::satoshi_per_bitcoin) + "}";
        }

        sendRequest("fundrawtransaction", params, [callback](const std::string& response) {
            json reply = json::parse(response);
            std::string error = reply["error"].empty() ? "" : reply["error"]["message"].get<std::string>();
            const auto& result = reply["result"];

            std::string hex;
            int changepos = -1;

            if (!result.empty())
            {
                hex = result["hex"].get<std::string>();
                changepos = result["changepos"].get<int>();
            }

            callback(error, hex, changepos);
        });
    }

    void Bitcoind016::signRawTransaction(const std::string& rawTx, std::function<void(const std::string&, const std::string&, bool)> callback)
    {
        sendRequest("signrawtransaction", "\"" + rawTx + "\"", [callback](const std::string& response) {
            json reply = json::parse(response);
            std::string error = reply["error"].empty() ? "" : reply["error"]["message"].get<std::string>();
            const auto& result = reply["result"];
            std::string hex;
            bool isComplete = false;
            if (!result.empty())
            {
                hex = result["hex"].get<std::string>();
                isComplete = result["complete"].get<bool>();
            }

            callback(error, hex, isComplete);
        });
    }

    void Bitcoind016::sendRawTransaction(const std::string& rawTx, std::function<void(const std::string&, const std::string&)> callback)
    {
        sendRequest("sendrawtransaction", "\"" + rawTx + "\"", [callback](const std::string& response) {
            json reply = json::parse(response);
            std::string error = reply["error"].empty() ? "" : reply["error"]["message"].get<std::string>();
            std::string result = reply["result"].empty() ? "" : reply["result"].get<std::string>();

            callback(error, result);
        });
    }

    void Bitcoind016::getRawChangeAddress(std::function<void(const std::string&, const std::string&)> callback)
    {
        sendRequest("getrawchangeaddress", "\"legacy\"", [callback](const std::string& response) {
            json reply = json::parse(response);
            std::string error = reply["error"].empty() ? "" : reply["error"]["message"].get<std::string>();
            auto result = reply["result"].empty() ? "" : reply["result"].get<std::string>();

            callback(error, result);
        });
    }

    void Bitcoind016::createRawTransaction(
        const std::string& withdrawAddress,
        const std::string& contractTxId,
        uint64_t amount,
        int outputIndex,
        Timestamp locktime,
        std::function<void(const std::string&, const std::string&)> callback)
    {
        std::string args("[{\"txid\": \"" + contractTxId + "\", \"vout\":" + std::to_string(outputIndex) + ", \"Sequence\": " + std::to_string(libbitcoin::max_input_sequence - 1) + " }]");

        args += ",{\"" + withdrawAddress + "\": " + std::to_string(double(amount) / libbitcoin::satoshi_per_bitcoin) + "}";
        if (locktime)
        {
            args += "," + std::to_string(locktime);
        }
        sendRequest("createrawtransaction", args, [callback](const std::string& response) {
            json reply = json::parse(response);
            std::string error = reply["error"].empty() ? "" : reply["error"]["message"].get<std::string>();
            auto result = reply["result"].empty() ? "" : reply["result"].get<std::string>();

            callback(error, result);
        });
    }

    void Bitcoind016::getTxOut(const std::string& txid, int outputIndex, std::function<void(const std::string&, const std::string&, double, uint16_t)> callback)
    {
        sendRequest("gettxout", "\"" + txid + "\"" + "," + std::to_string(outputIndex), [callback](const std::string& response) {
            json reply = json::parse(response);
            std::string error = reply["error"].empty() ? "" : reply["error"]["message"].get<std::string>();
            const auto& result = reply["result"];
            double value = 0;
            uint16_t confirmations = 0;
            std::string scriptHex;

            if (!result.empty())
            {
                scriptHex = result["scriptPubKey"]["hex"].get<std::string>();
                value = result["value"].get<double>();
                confirmations = result["confirmations"];
            }

            callback(error, scriptHex, value, confirmations);
        });
    }

    void Bitcoind016::getBlockCount(std::function<void(const std::string&, uint64_t)> callback)
    {
        sendRequest("getblockcount", "", [callback](const std::string& response) {
            json reply = json::parse(response);
            std::string error = reply["error"].empty() ? "" : reply["error"]["message"].get<std::string>();
            uint64_t blockCount = reply["result"].empty() ? 0 : reply["result"].get<uint64_t>();

            callback(error, blockCount);
        });
    }

    uint8_t Bitcoind016::getAddressVersion()
    {
        if (isMainnet())
        {
            return libbitcoin::wallet::ec_private::mainnet_wif;
        }
        
        return libbitcoin::wallet::ec_private::testnet_wif;
    }

    Amount Bitcoind016::getFeeRate() const
    {
        return m_feeRate;
    }

    Amount Bitcoind016::getTxMinConfirmations() const
    {
        return m_confirmations;
    }

    void Bitcoind016::sendRequest(const std::string& method, const std::string& params, std::function<void(const std::string&)> callback)
    {
        const std::string content(R"({"method":")" + method + R"(","params":[)" + params + "]}");
        const HeaderPair headers[] = {
            {"Authorization", m_authorization.data()}
        };
        HttpClient::Request request;

        request.address(m_address)
            .connectTimeoutMsec(2000)
            .pathAndQuery("/")
            .headers(headers)
            .numHeaders(1)
            .method("POST")
            .body(content.c_str(), content.size());

        request.callback([callback](uint64_t id, const HttpMsgReader::Message& msg) -> bool {
            size_t sz = 0;
            const void* body = msg.msg->get_body(sz);
            if (sz > 0 && body)
            {
                callback(std::string(static_cast<const char*>(body), sz));
            }
            else
            {
                callback("");
            }
            return false;
        });

        m_httpClient.send_request(request);
    }

    bool Bitcoind016::isMainnet() const
    {
        return m_isMainnet;
    }
}