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
#include "utility/helpers.h"
#include "utility/logger.h"

using namespace beam;

namespace {

const uint16_t PORT = 8765;
io::AsyncEvent::Trigger g_stopEvent;

class DummyHttpServer {
public:
    DummyHttpServer(io::Reactor& reactor) :
        _msgCreator(2000),
        _reactor(reactor)
    {
        _server = io::TcpServer::create(
            _reactor,
            io::Address::localhost().port(PORT),
            BIND_THIS_MEMFN(on_stream_accepted)
        );
    }
private:
    void on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode) {
        if (errorCode == 0) {
            LOG_DEBUG() << "Stream accepted";
            _theConnection = std::make_unique<HttpConnection>(
                222,
                BaseConnection::inbound,
                BIND_THIS_MEMFN(on_request),
                10000,
                1024,
                std::move(newStream)
            );
        } else {
            LOG_ERROR() << "Server error " << io::error_str(errorCode);
            g_stopEvent();
        }
    }

    bool on_request(uint64_t, const HttpMsgReader::Message& msg) {
        if (msg.what != HttpMsgReader::http_message || !msg.msg) {
            LOG_ERROR() << "Request error";
            g_stopEvent();
        }

        const std::string& path = msg.msg->get_path();

        // Create http response
        io::SharedBuffer body;
        int code = 200;
        const char* message = "OK";
        bool stop = (path == "/stop");
        if (!stop && path != "/") {
            try {
                body = io::map_file_read_only(path.c_str());
            } catch (const std::exception& e) {
                LOG_DEBUG() << e.what();
                code = 404;
                message = "Not found";
            }
        }

        static const HeaderPair headers[] = {
            {"Server", "DummyHttpServer"},
            {"Host", msg.msg->get_header("host").c_str() }
        };

        if (_theConnection && _msgCreator.create_response(
            _serialized,
            code,
            message,
            headers,
            sizeof(headers) / sizeof(HeaderPair),
            1,
            "text/plain",
            body
        )) {
            _theConnection->write_msg(_serialized);
            _theConnection->shutdown();
        } else {
            LOG_ERROR() << "Cannot create response";
            g_stopEvent();
        }

        _theConnection.reset();
        if (stop) {
            g_stopEvent();
        }
        return false;
    }

    HttpMsgCreator _msgCreator;
    io::SerializedMsg _serialized;
    io::Reactor& _reactor;
    io::TcpServer::Ptr _server;
    HttpConnection::Ptr _theConnection;
};

int http_server_test() {
    int nErrors = 0;

    try {
        io::Reactor::Ptr reactor = io::Reactor::create();
        io::AsyncEvent::Ptr stopEvent = io::AsyncEvent::create(*reactor, [&reactor]() {reactor->stop();});
        g_stopEvent = stopEvent;
        DummyHttpServer server(*reactor);
        reactor->run();
    } catch (const std::exception& e) {
        LOG_ERROR() << e.what();
        nErrors = 255;
    }

    return nErrors;
}

} //namespace

int main() {
    http_server_test();
}
