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

#include "p2p/http_connection.h"
#include "p2p/http_msg_creator.h"
#include "utility/io/tcpserver.h"
#include "utility/io/asyncevent.h"
#include "utility/io/timer.h"
#include "utility/helpers.h"
#include "utility/logger.h"

#include "bitcoin/bitcoin.hpp"

#include <iostream>

using namespace beam;

namespace 
{
io::AsyncEvent::Trigger g_stopEvent;
const uint16_t PORT = 18443;

class DummyHttpClient {
public:
    DummyHttpClient(io::Reactor& reactor) :
        _msgCreator(1000),
        _reactor(reactor),
        _timer(io::Timer::create(_reactor))
    {
        _timer->start(100, false, BIND_THIS_MEMFN(on_timer));
    }

    int uncompleted = 1;

private:
    void on_timer() {
        if (!_reactor.tcp_connect(io::Address::localhost().port(PORT), 333, BIND_THIS_MEMFN(on_connected), 1000)) {
            LOG_ERROR() << "Connect failed";
            g_stopEvent();
        }
    }

    void on_connected(uint64_t, io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode) {
        if (errorCode != 0) {
            LOG_ERROR() << "Connect failed, " << io::error_str(errorCode);
            g_stopEvent();
            return;
        }

        _theConnection = std::make_unique<HttpConnection>(
            222,
            BaseConnection::outbound,
            BIND_THIS_MEMFN(on_response),
            10 * 1024 * 1024,
            1024,
            std::move(newStream)
            );

        std::string userWithPass("test:123");
        libbitcoin::data_chunk t(userWithPass.begin(), userWithPass.end());

        LOG_INFO() << std::string("Basic ") + libbitcoin::encode_base64(t);
        //"test:123"
        static const HeaderPair headers[] = {
            {"Host", "127.0.0.1" },
            {"Connection", "close"},
            {"Authorization", (std::string("Basic ") + libbitcoin::encode_base64(t)).c_str()}
        };

        const char *data = "{\"method\":\"getblockchaininfo\",\"params\":[],\"id\":1}\n";
            //"{\"jsonrpc\": \"1.0\", \"id\":\"curltest\", \"method\": \"getblockchaininfo\", \"params\": [] }";

        if (_msgCreator.create_request(
            _serialized,
            "POST",
            "/",
            headers, 3, 1,
            data, strlen(data)
        )) {
            _theConnection->write_msg(_serialized);
            _serialized.clear();
        }
        else {
            LOG_ERROR() << "Cannot send request";
            g_stopEvent();
        }
    }

    bool on_response(uint64_t, const HttpMsgReader::Message& msg) {
        LOG_INFO() << "on_response";
        if (msg.what != HttpMsgReader::http_message || !msg.msg) {
            LOG_INFO() << "not http: " << msg.what;
            if (msg.what == HttpMsgReader::connection_error && msg.connectionError == io::EC_EOF) {
                _theConnection.reset();
            }
            else {
                g_stopEvent();
            }
            return false;
        }

        size_t bodySize = 0;
        const void* body = msg.msg->get_body(bodySize);
        if (bodySize) {
            std::cout << body << std::endl;
            /*try {
                io::SharedBuffer fileContent = io::map_file_read_only(THIS_PATH.c_str());
                if (fileContent.size == bodySize && !memcmp(body, fileContent.data, bodySize)) {
                    uncompleted = 0;
                    LOG_INFO() << "file received successfully, size=" << bodySize;
                }
                else {
                    LOG_INFO() << "file receive error" << TRACE(bodySize) << TRACE(fileContent.size);
                }
            }
            catch (const std::exception& e) {
                LOG_ERROR() << e.what();
            }*/
        }

        _theConnection.reset();
        g_stopEvent();
        return false;
    }

    HttpMsgCreator _msgCreator;
    io::SerializedMsg _serialized;
    io::Reactor& _reactor;
    HttpConnection::Ptr _theConnection;
    io::Timer::Ptr _timer;
};
}

int main() {
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = Logger::create(logLevel, logLevel);

    try {
        io::Reactor::Ptr reactor = io::Reactor::create();
        io::AsyncEvent::Ptr stopEvent = io::AsyncEvent::create(*reactor, [&reactor]() {reactor->stop(); });
        g_stopEvent = stopEvent;
        DummyHttpClient client(*reactor);
        reactor->run();
    }
    catch (const std::exception& e) {
        LOG_ERROR() << e.what();
    }

    return 0;
}