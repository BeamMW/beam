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
#include "utility/io/timer.h"
#include "utility/io/tcpserver.h"
#include "utility/io/asyncevent.h"
#include "utility/helpers.h"
#include "wallet/bitcoin_rpc.h"
#include "nlohmann/json.hpp"

using namespace beam;
using json = nlohmann::json;
const uint16_t PORT = 18443;
io::AsyncEvent::Trigger g_stopEvent;

void testWithBitcoinD()
{
    
    io::Reactor::Ptr reactor = io::Reactor::create();
    io::Timer::Ptr timer(io::Timer::create(*reactor));

    timer->start(5000, false, [&reactor]() {
        reactor->stop();
    });

    io::Address addr(io::Address::localhost(), PORT);
    BitcoinRPC rpc(*reactor, "test", "123", addr);

    rpc.getBlockchainInfo([](const std::string& result) {
        LOG_INFO() << "getblockchaininfo result: " << result;
        
        return false;
    });

    rpc.getNetworkInfo([](const std::string& result) {
        LOG_INFO() << "getnetworkinfo result: " << result;

        return false;
    });

    rpc.getWalletInfo([](const std::string& result) {
        LOG_INFO() << "getwalletinfo result: " << result;

        return false;
    });

    rpc.estimateFee(3, [](const std::string& result) {
        LOG_INFO() << "estimateFee result: " << result;

        return false;
    });

    rpc.getRawChangeAddress([](const std::string& result) {
        LOG_INFO() << "getRawChangeAddress result: " << result;

        return false;
    });

    rpc.getBalance([](const std::string& result) {
        LOG_INFO() << "getbalance result: " << result;

        return false;
    });

    rpc.dumpPrivKey("mhYozJvft4LdsTkGbsXEAifSmcNMNsqFpF", [](const std::string& result) {
        LOG_INFO() << "dumpprivkey result: " << result;
        return false;
    });

    reactor->run();
}


class BitcoinHttpServer
{
public:
    BitcoinHttpServer(io::Reactor& reactor)
        : m_reactor(reactor)
        , m_msgCreator(1000)
        , m_lastId(0)
    {
        m_server = io::TcpServer::create(
            m_reactor,
            io::Address::localhost().port(PORT),
            BIND_THIS_MEMFN(onStreamAccepted)
        );
    }

private:

    void onStreamAccepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode) 
    {
        if (errorCode == 0) 
        {
            LOG_DEBUG() << "Stream accepted";
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
            g_stopEvent();
        }
    }

    bool onRequest(uint64_t peerId, const HttpMsgReader::Message& msg) 
    {
        const char* message = "OK";
        static const HeaderPair headers[] = 
        {
            {"Server", "BitcoinHttpServer"}            
        };
        io::SharedBuffer body = generateResponse(msg);
        io::SerializedMsg serialized;

        if (m_connections[peerId] && m_msgCreator.create_response(
            serialized, 200, message, headers, sizeof(headers) / sizeof(HeaderPair),
            1, "text/plain", body.size)) 
        {
            serialized.push_back(body);
            m_connections[peerId]->write_msg(serialized);
            m_connections[peerId]->shutdown();
        }
        else
        {
            LOG_ERROR() << "Cannot create response";
            g_stopEvent();
        }

        m_connections.erase(peerId);
        
        return false;
    }

    io::SharedBuffer generateResponse(const HttpMsgReader::Message& msg)
    {
        size_t sz = 0;
        const void* rawReq = msg.msg->get_body(sz);
        std::string result;
        if (sz > 0 && rawReq)
        {
            std::string req(static_cast<const char*>(rawReq), sz);
            json j = json::parse(req);
            if (j["method"] == "getbalance")
                result = R"({"result":12684.40000000,"error":null,"id":null})";
            else if (j["method"] == "dumpprivkey")
                result = R"({"result":"cTZEjMtL96FyC43AxEvUxbs3pinad2cH8wvLeeCYNUwPURqeknkG","error":null,"id":null})";
            else if (j["method"] == "getblockchaininfo")
                result = R"({"result":{"chain":"regtest","blocks":518,"headers":518,"bestblockhash":"7eaba692dd206610003929c6a4104fa38fffbbc8bf521c0b15d26d11238926f6","difficulty":4.656542373906925e-010,"mediantime":1538724935,"verificationprogress":1,"initialblockdownload":true,"chainwork":"000000000000000000000000000000000000000000000000000000000000040e","size_on_disk":165279,"pruned":false,"softforks":[{"id":"bip34","version":2,"reject":{"status":false}},{"id":"bip66","version":3,"reject":{"status":false}},{"id":"bip65","version":4,"reject":{"status":false}}],"bip9_softforks":{"csv":{"status":"active","startTime":0,"timeout":9223372036854775807,"since":432},"segwit":{"status":"active","startTime":-1,"timeout":9223372036854775807,"since":0}},"warnings":""},"error":null,"id":null})";
            else if (j["method"] == "getnetworkinfo")
                result = R"( {"result":{"version":160300,"subversion":"/Satoshi:0.16.3/","protocolversion":70015,"localservices":"000000000000040d","localrelay":true,"timeoffset":0,"networkactive":true,"connections":0,"networks":[{"name":"ipv4","limited":false,"reachable":true,"proxy":"","proxy_randomize_credentials":false},{"name":"ipv6","limited":false,"reachable":true,"proxy":"","proxy_randomize_credentials":false},{"name":"onion","limited":true,"reachable":false,"proxy":"","proxy_randomize_credentials":false}],"relayfee":0.00001000,"incrementalfee":0.00001000,"localaddresses":[],"warnings":""},"error":null,"id":null})";
            else if (j["method"] == "getwalletinfo")
                result = R"( {"result":{"walletname":"wallet.dat","walletversion":159900,"balance":12684.40000000,"unconfirmed_balance":0.00000000,"immature_balance":818.85000000,"txcount":529,"keypoololdest":1538032587,"keypoolsize":1000,"keypoolsize_hd_internal":1000,"paytxfee":0.00000000,"hdmasterkeyid":"d52b96aa5b7cf7de7ba49a56d2d04ede460efe3f"},"error":null,"id":null})";
            else if (j["method"] == "getrawchangeaddress")
                result = R"( {"result":"2NB9nqKnHgThByiSzVEVDg5cYC2HwEMBcEK","error":null,"id":null})";
        }
        else
        {
            LOG_ERROR() << "Request is wrong";
            g_stopEvent();
        }

        io::SharedBuffer body;

        body.assign(result.data(), result.size());
        return body;
    }

private:
    io::Reactor& m_reactor;
    io::TcpServer::Ptr m_server;
    std::map<uint64_t, HttpConnection::Ptr> m_connections;
    HttpMsgCreator m_msgCreator;
    uint64_t m_lastId;
};

void testWithFakeServer()
{
    io::Reactor::Ptr reactor = io::Reactor::create();
    io::Timer::Ptr timer(io::Timer::create(*reactor));

    timer->start(5000, false, [&reactor]() {
        reactor->stop();
    });

    BitcoinHttpServer httpServer(*reactor);

    io::Address addr(io::Address::localhost(), PORT);
    BitcoinRPC rpc(*reactor, "test", "123", addr);

    rpc.getBalance([](const std::string& result) {
        LOG_INFO() << "getbalance result: " << result;
        return false;
    });

    rpc.dumpPrivKey("mhYozJvft4LdsTkGbsXEAifSmcNMNsqFpF", [](const std::string& result) {
        LOG_INFO() << "dumpprivkey result: " << result;
        return false;
    });

    rpc.getBlockchainInfo([](const std::string& result) {
        LOG_INFO() << "getblockchaininfo result: " << result;
        return false;
    });

    rpc.getNetworkInfo([](const std::string& result) {
        LOG_INFO() << "getnetworkinfo result: " << result;
        return false;
    });

    rpc.getWalletInfo([](const std::string& result) {
        LOG_INFO() << "getwalletinfo result: " << result;
        return false;
    });

    rpc.getRawChangeAddress([](const std::string& result) {
        LOG_INFO() << "getRawChangeAddress result: " << result;
        return false;
    });

    reactor->run();
}

int main()
{
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = beam::Logger::create(logLevel, logLevel);

    //testWithBitcoinD();
    testWithFakeServer();
    return 0;
}