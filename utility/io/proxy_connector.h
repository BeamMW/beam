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

#pragma once

#include "utility/io/reactor.h"
#include "utility/io/tcpstream.h"


namespace beam {
namespace io {

class ProxyConnector {
public:
    using OnConnect = Reactor::ConnectCallback;
    using OnResponse = std::function<bool(  ProxyConnector& instance,   // implicit this pass
                                            uint64_t tag,
                                            ErrorCode errorCode,
                                            void *data,
                                            size_t size)>;

    ProxyConnector(Reactor&);
    // ~ProxyConnector(); // TODO: proxy

    OnConnect create_connection(uint64_t tag,   // unique identifier
                                Address destination,
                                const OnConnect& onProxyEstablish,
                                int timeoutMsec,
                                bool tlsConnect);

    void cancel_all() {}; // TODO: proxy
    void destroy_connect_timer_if_needed() {};
    void cancel_tcp_connect(uint64_t tag) {};

private:
    struct ProxyConnectRequest {
        using Ptr = std::unique_ptr<ProxyConnectRequest>;

        uint64_t tag;
        beam::io::Address destination;
        OnConnect on_connection_establish;
        OnResponse on_proxy_response;
        int timeoutMsec;
        bool tlsConnect;
        TcpStream::Ptr stream;
    };

    void delete_connection(uint64_t tag);

    void on_tcp_connect(uint64_t tag, std::unique_ptr<TcpStream>&& new_stream, ErrorCode errorCode);
    void send_auth_methods(uint64_t tag);
    bool on_auth_method_resp(uint64_t tag, ErrorCode errorCode, void* data, size_t size);
    void send_connect_request(uint64_t tag);
    bool on_connect_resp(uint64_t tag, ErrorCode errorCode, void* data, size_t size);
    void on_connection_established(uint64_t tag);

    Reactor& _reactor;
    MemPool<ProxyConnectRequest, sizeof(ProxyConnectRequest)> _connectRequestsPool;
    std::unordered_map<uint64_t, ProxyConnectRequest*> _connectRequests;
};

}   // namespace beam
}   // namespace io
