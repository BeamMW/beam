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
#include "tcpstream.h"
#include "address.h"

namespace beam { namespace io {

class TcpServer : protected Reactor::Object {
public:
    using Ptr = std::unique_ptr<TcpServer>;

    // Either newStream is accepted or status != 0
    using Callback = std::function<void(TcpStream::Ptr&& newStream, ErrorCode status)>;

    // TODO simplified API, will add more as soon as needed
    static Ptr create(Reactor& reactor, Address bindAddress, Callback&& callback);

private:
    TcpServer(Callback&& callback);

    void on_accept(ErrorCode errorCode);

    Callback _callback;
};

}} //namespaces

