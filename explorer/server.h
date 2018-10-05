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

#include "adapter.h"
#include "p2p/http_connection.h"
#include "p2p/http_msg_creator.h"
#include "utility/io/tcpserver.h"
#include "utility/io/asyncevent.h"
#include "utility/io/coarsetimer.h"
#include "utility/helpers.h"

namespace beam {

class StatusServer {
public:
    using BlockCallback = std::function<bool(DummyBlock&)>;

    StatusServer(io::Reactor& reactor, io::Address bindAddress, BlockCallback blockCallback);

    void update_status(const DummyStatus& ds);

private:
    void start_server();

    void on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode);

    bool on_request(uint64_t id, const HttpMsgReader::Message& msg);
    bool send_status(const HttpConnection::Ptr& conn);
    bool send_blocks(const HttpConnection::Ptr& conn);
    bool send_404(const HttpConnection::Ptr& conn);
    bool send(const HttpConnection::Ptr& conn, int code, const char* message, const io::SharedBuffer* body);

    HttpMsgCreator _msgCreator;
    io::SerializedMsg _serialized;
    io::Reactor& _reactor;
    io::MultipleTimers _timers;
    io::Address _bindAddress;
    BlockCallback _blockCallback;
    io::TcpServer::Ptr _server;
    std::map<uint64_t, HttpConnection::Ptr> _connections;
    HttpUrl _currentUrl;

    //TODO
    //DummyBlock db;
    io::SharedBuffer _statusBody;
};

} //namespace
