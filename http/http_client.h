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

#pragma once

#include "http/http_connection.h"
#include "http/http_msg_creator.h"

namespace beam {

/// Http async client supporting multiple requests and keep-alive
class HttpClient {
public:
    /// Returns true to keep connection alive, false to close connection
    using OnResponse = std::function<bool(uint64_t id, const HttpMsgReader::Message& msg)>;

    struct Request {

        // If id != 0 then address and connectTimeoutMsec fields are ignored
        // and the request will be sent over established connection with this id
        uint64_t id_;

        io::Address address_;
        unsigned connectTimeoutMsec_;
        OnResponse callback_;
        const char* method_;
        const char* pathAndQuery_;
        const HeaderPair* headers_;
        size_t numHeaders_;
        const char* contentType_;
        io::SerializedMsg body_;

#define SET_PARAM(Name) Request& Name(const decltype(Request::Name ## _)& x) { Name ## _ = x; return *this; }
        SET_PARAM(id)
        SET_PARAM(address)
        SET_PARAM(connectTimeoutMsec)
        SET_PARAM(callback)
        SET_PARAM(method)
        SET_PARAM(pathAndQuery)
        SET_PARAM(headers)
        SET_PARAM(numHeaders)
        SET_PARAM(contentType)
#undef SET_PARAM

        Request& body(const void* data, size_t size) {
            body_.clear();
            if (data && size) {
                body_.push_back(io::SharedBuffer(data, size));
            }
            return *this;
        }

        Request& body(const io::SharedBuffer& buf) {
            body_.clear();
            if (!buf.empty()) {
                body_.push_back(buf);
            }
            return *this;
        }

        Request& body(const io::SerializedMsg& msg) {
            body_ = msg;
            return *this;
        }

        Request() { reset(); }

        void reset() {
            id(0).address(io::Address()).connectTimeoutMsec(10000).callback(OnResponse())
                .method("GET").pathAndQuery(0).headers(0).numHeaders(0).contentType("");
            body_.clear();
        }

        bool validate() const {
            if (id_ == 0 && (address_.ip() == 0 || address_.port() == 0)) return false;
            if (!callback_ || !method_ || !pathAndQuery_) return false;
            if (headers_ == 0 && numHeaders_ > 0) return false;
            return (!(contentType_ == 0 && !body_.empty()));
        }
    };

    explicit HttpClient(io::Reactor& reactor);

    ~HttpClient();

    /// Sends request asynchronously, Returns connection ID (>0) or error
    expected<uint64_t, io::ErrorCode> send_request(const Request& request);

    /// Cancels request, MUST be called if the caller goes out of scope
    void cancel_request(uint64_t id);

private:


    void on_connected(uint64_t tag, io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode);

    bool on_response(uint64_t id, const HttpMsgReader::Message& msg);

    struct Ctx {
        io::SerializedMsg unsent;
        HttpConnection::Ptr conn;
        OnResponse callback;
    };

    io::Reactor& _reactor;
    HttpMsgCreator _msgCreator;
    std::map<uint64_t, Ctx> _connections;
    std::map<uint64_t, uint64_t> _pendingConnections;
    uint64_t _idCounter;
};

} //namespace
