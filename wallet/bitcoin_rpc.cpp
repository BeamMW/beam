// Copyright 2018 The Beam Team
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

#include "utility/logger.h"
#include "bitcoin_rpc.h"
#include "bitcoin/bitcoin.hpp"

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

    BitcoinRPC::BitcoinRPC(io::Reactor& reactor, const std::string& userName, const std::string& pass, const io::Address& address)
        : m_httpClient(reactor)
        , m_address(address)
        , m_authorization(generateAuthorization(userName, pass))
    {        
    }

    void BitcoinRPC::getBlockchainInfo(OnResponse callback)
    {
        sendRequest("getblockchaininfo", "", callback);
    }

    void BitcoinRPC::dumpPrivKey(const std::string& btcAddress, OnResponse callback)
    {
        sendRequest("dumpprivkey", "\"" + btcAddress + "\"", callback);
    }

    void BitcoinRPC::fundRawTransaction(const std::string& rawTx, OnResponse callback)
    {
        sendRequest("fundrawtransaction", "\"" + rawTx + "\"", callback);
    }

    void BitcoinRPC::signRawTransaction(OnResponse callback)
    {
        //sendRequest("signrawtransactionwithwallet", "\"" + rawTx + "\"", callback);
    }

    void BitcoinRPC::sendRawTransaction(const std::string& rawTx, OnResponse callback)
    {
        sendRequest("sendrawtransaction", "\"" + rawTx + "\"", callback);
    }

    void BitcoinRPC::getNetworkInfo(OnResponse callback)
    {
        sendRequest("getnetworkinfo", "", callback);
    }

    void BitcoinRPC::getWalletInfo(OnResponse callback)
    {
        sendRequest("getwalletinfo", "", callback);
    }

    void BitcoinRPC::estimateFee(int blocks, OnResponse callback)
    {
        sendRequest("estimatefee", "\"" + std::to_string(blocks) + "\"", callback);
    }

    void BitcoinRPC::getRawChangeAddress(OnResponse callback)
    {
        sendRequest("getrawchangeaddress", "", callback);
    }

    void BitcoinRPC::createRawTransaction(OnResponse callback)
    {
        //sendRequest("getrawchangeaddress", "", callback);
    }

    void BitcoinRPC::getRawTransaction(const std::string& txid, OnResponse callback)
    {
        sendRequest("getrawtransaction", "\"" + txid + "\"", callback);
    }

    void BitcoinRPC::getBalance(OnResponse callback)
    {
        sendRequest("getbalance", "", callback);
    }

    void BitcoinRPC::sendRequest(const std::string& method, const std::string& params, OnResponse callback)
    {
        const std::string content(R"({"method":")" + method + R"(","params":[)" + params + "]}");
        const HeaderPair headers[] = {
            {"Authorization", m_authorization.data()}
        };
        HttpClient::Request request;
        
        LOG_INFO() << content;

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
}