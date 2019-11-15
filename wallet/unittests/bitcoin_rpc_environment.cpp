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

#include "utility/logger.h"
#include "utility/io/timer.h"
#include "http/http_connection.h"
#include "http/http_msg_creator.h"
#include "utility/helpers.h"
#include "nlohmann/json.hpp"

#include "3rdparty/libbitcoin/include/bitcoin/bitcoin.hpp"

const uint16_t PORT = 13300;
const std::string btcUserName = "Alice";
const std::string btcPass = "123";

using namespace beam;
using json = nlohmann::json;

class BitcoinHttpServer
{
public:
    BitcoinHttpServer(const std::string& userName = btcUserName, const std::string& pass = btcPass)
        : m_reactor(io::Reactor::get_Current())
        , m_msgCreator(1000)
        , m_lastId(0)
        , m_userName(userName)
        , m_pass(pass)
    {
        m_server = io::TcpServer::create(
            m_reactor,
            io::Address::localhost().port(PORT),
            BIND_THIS_MEMFN(onStreamAccepted)
        );
    }

protected:

    virtual std::string fundRawTransaction()
    {
        return R"({"result":{"hex":"2NB9nqKnHgThByiSzVEVDg5cYC2HwEMBcEK", "fee": 0, "changepos": 0},"error":null,"id":null})";
    }

    virtual std::string signRawTransaction()
    {
        return R"({"result": {"hex": "2NB9nqKnHgThByiSzVEVDg5cYC2HwEMBcEK", "complete": true},"error":null,"id":null})";
    }

    virtual std::string sendRawTransaction()
    {
        return R"({"result":"2NB9nqKnHgThByiSzVEVDg5cYC2HwEMBcEK","error":null,"id":null})";
    }

    virtual std::string getRawChangeAddress()
    {
        return R"({"result":"2NB9nqKnHgThByiSzVEVDg5cYC2HwEMBcEK","error":null,"id":null})";
    }

    virtual std::string createRawTransaction()
    {
        return R"({"result": "2NB9nqKnHgThByiSzVEVDg5cYC2HwEMBcEK","error":null,"id":null})";
    }

    virtual std::string getTxOut()
    {
        return R"( {"result":{"confirmations":2,"value":0.4,"scriptPubKey":{"hex":"2NB9nqKnHgThByiSzVEVDg5cYC2HwEMBcEK"}},"error":null,"id":null})";
    }

    virtual std::string getBlockCount()
    {
        return R"( {"result":2,"error":null,"id":null})";
    }

    virtual std::string getBalance()
    {
        return R"({"result":12684.40000000,"error":null,"id":null})";
    }

private:

    void onStreamAccepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode)
    {
        if (errorCode == 0)
        {
            uint64_t peerId = m_lastId++;
            m_connections[peerId] = std::make_unique<HttpConnection>(
                peerId,
                BaseConnection::inbound,
                BIND_THIS_MEMFN(onRequest),
                10000,
                1024,
                std::move(newStream)
                );
        }
        else
        {
            LOG_ERROR() << "Server error " << io::error_str(errorCode);
            stopServer();
        }
    }

    bool onRequest(uint64_t peerId, const HttpMsgReader::Message& msg)
    {
        const char* message = "OK";
        static const HeaderPair headers[] =
        {
            {"Server", "BitcoinHttpServer"}
        };

        std::string result;
        int responseStatus = 200;
        bitcoin::BitcoinCoreSettings settings{ m_userName, m_pass, io::Address{} };

        if (msg.msg->get_header("Authorization") == settings.generateAuthorization())
        {
            size_t sz = 0;
            const void* rawReq = msg.msg->get_body(sz);

            if (sz > 0 && rawReq)
            {
                std::string str(static_cast<const char*>(rawReq), sz);
                result = generateResponse(str);
            }
            else
            {
                LOG_ERROR() << "Request is wrong";
                stopServer();
            }
        }
        else
        {
            responseStatus = 401;
        }

        io::SharedBuffer body;

        body.assign(result.data(), result.size());
        io::SerializedMsg serialized;

        if (m_connections[peerId] && m_msgCreator.create_response(
            serialized, responseStatus, message, headers, sizeof(headers) / sizeof(HeaderPair),
            1, "text/plain", body.size))
        {
            serialized.push_back(body);
            m_connections[peerId]->write_msg(serialized);
            m_connections[peerId]->shutdown();
        }
        else
        {
            LOG_ERROR() << "Cannot create response";
            stopServer();
        }

        m_connections.erase(peerId);

        return false;
    }

    std::string generateResponse(const std::string& msg)
    {
        json j = json::parse(msg);
        if (j["method"] == "getbalance")
            return getBalance();
        else if (j["method"] == "fundrawtransaction")
            return fundRawTransaction();
        else if (j["method"] == "signrawtransaction")
            return signRawTransaction();
        else if (j["method"] == "sendrawtransaction")
            return sendRawTransaction();
        else if (j["method"] == "getrawchangeaddress")
            return getRawChangeAddress();
        else if (j["method"] == "createrawtransaction")
            return createRawTransaction();
        else if (j["method"] == "gettxout")
            return getTxOut();
        else if (j["method"] == "getblockcount")
            return getBlockCount();
        return "";
    }

    void stopServer()
    {
        m_reactor.stop();
    }

private:
    io::Reactor& m_reactor;
    io::TcpServer::Ptr m_server;
    std::map<uint64_t, HttpConnection::Ptr> m_connections;
    HttpMsgCreator m_msgCreator;
    uint64_t m_lastId;
    std::string m_userName;
    std::string m_pass;
};

class BitcoinHttpServerEmptyResult : public BitcoinHttpServer
{
private:
    std::string fundRawTransaction() override
    {
        return R"({"error":null,"id":null})";
    }

    std::string getTxOut() override
    {
        return R"( {"result":null,"error":null,"id":null})";
    }

    std::string getBlockCount() override
    {
        return R"( {"error":null,"id":null})";
    }

    std::string getBalance() override
    {
        return R"({"result":null,"error":null,"id":null})";
    }
};

class BitcoinHttpServerEmptyResponse : public BitcoinHttpServer
{
private:
    std::string fundRawTransaction() override
    {
        return "";
    }
};