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
#include "tcpserver.h"
#include "sslio.h"

namespace beam { namespace io {

class SslServer : public TcpServer {
public:
    /// Creates the server and starts listening
    static Ptr create(Reactor& reactor, Address bindAddress, Callback&& callback,
                      const char* certFileName, const char* privKeyFileName);

    ~SslServer() = default;

private:
    SslServer(Callback&& callback, Reactor& reactor, Address bindAddress, SSLContext::Ptr&& ctx);

    void on_accept(ErrorCode errorCode) override;

    SSLContext::Ptr _ctx;
};

}} //namespaces
