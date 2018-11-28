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

#include "http_client.h"
#include "utility/helpers.h"
#include "utility/logger.h"

namespace beam {

static const size_t CREATOR_FRAGMENT_SIZE = 1000;
static const size_t READER_FRAGMENT_SIZE = 8192;
static const size_t MAX_RESPONSE_BODY_SIZE = 16*1024*1024;

HttpClient::HttpClient(io::Reactor& reactor) :
    _reactor(reactor),
    _msgCreator(CREATOR_FRAGMENT_SIZE),
    _idCounter(0)
{}

HttpClient::~HttpClient() {
    for (auto& p : _pendingConnections) {
        _reactor.cancel_tcp_connect(p.first);
    }
}

expected<uint64_t, io::ErrorCode> HttpClient::send_request(const HttpClient::Request& request) {
    if (!request.validate()) return make_unexpected(io::EC_EINVAL);
    Ctx* ctx = 0;
    uint64_t id = 0;
    bool newConnection = false;
    if (request.id_ != 0) {
        auto it = _connections.find(request.id_);
        if (it == _connections.end()) return make_unexpected(io::EC_EINVAL);
        ctx = &it->second;
        id = request.id_;
    } else {
        id = ++_idCounter;
        ctx = &_connections[id];
        newConnection = true;
    }
    size_t bodySize = 0;
    if (!request.body_.empty()) {
        for (auto& f : request.body_) bodySize += f.size;
    }

    if (!_msgCreator.create_request(
        ctx->unsent,
        request.method_,
        request.pathAndQuery_,
        request.headers_,
        request.numHeaders_,
        1, //http/1.1
        request.contentType_,
        bodySize
    )) {
        if (!newConnection && !ctx->conn) {
            _reactor.cancel_tcp_connect(uint64_t(ctx));
        }
        _connections.erase(id);
        return make_unexpected(io::EC_EINVAL);
    }

    if (bodySize) {
        ctx->unsent.insert(ctx->unsent.end(), request.body_.begin(), request.body_.end());
    }

    io::Result result;
    if (ctx->conn) {
        result = ctx->conn->write_msg(ctx->unsent, true);
    } else if (newConnection) {
        int timeout = (request.connectTimeoutMsec_ > 0) ? int(request.connectTimeoutMsec_) : -1;
        auto tag = uint64_t(ctx);
        result = _reactor.tcp_connect(request.address_, tag, BIND_THIS_MEMFN(on_connected), timeout);
        if (result) {
            _pendingConnections[tag] = id;
        }
    }

    if (!result) {
        _connections.erase(id);
        return make_unexpected(result.error());
    }

    ctx->callback = request.callback_;
    return id;
}

void HttpClient::cancel_request(uint64_t id) {
    auto it = _connections.find(id);
    if (it != _connections.end()) {
        auto tag = uint64_t(&it->second);
        if (_pendingConnections.count(tag)) {
            _reactor.cancel_tcp_connect(tag);
            _pendingConnections.erase(tag);
        }
        _connections.erase(it);
    }
}

void HttpClient::on_connected(uint64_t tag, io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode) {
    auto it1 = _pendingConnections.find(tag);
    if (it1 == _pendingConnections.end()) return;
    uint64_t id = it1->second;
    _pendingConnections.erase(it1);

    auto it2 = _connections.find(id);
    if (it2 == _connections.end()) return;
    Ctx& ctx = it2->second;

    assert(tag == uint64_t(&ctx));
    assert(ctx.callback);

    if (errorCode != io::EC_OK) {
        ctx.callback(id, HttpMsgReader::Message(errorCode));
        _connections.erase(it2);
        return;
    }

    HttpConnection::Ptr conn = std::make_unique<HttpConnection>(
        id,
        BaseConnection::outbound,
        BIND_THIS_MEMFN(on_response),
        MAX_RESPONSE_BODY_SIZE,
        READER_FRAGMENT_SIZE,
        std::move(newStream)
    );

    if (!ctx.unsent.empty()) {
        auto result = conn->write_msg(ctx.unsent, true);
        if (!result) {
            ctx.callback(id, HttpMsgReader::Message(result.error()));
            _connections.erase(it2);
        } else {
            ctx.conn = std::move(conn);
        }
    }
}

bool HttpClient::on_response(uint64_t id, const HttpMsgReader::Message& msg) {
    auto it = _connections.find(id);
    if (it == _connections.end()) return false;

    bool proceed = it->second.callback(id, msg);
    if (msg.what != HttpMsgReader::http_message) proceed = false;

    if (!proceed) {
        _connections.erase(id);
    }
    return proceed;
}

} //namespace
