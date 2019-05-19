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
#include "sslio.h"

namespace beam { namespace io {

class SslStream : public TcpStream {
public:
    ~SslStream() = default;

    /// Writes raw data, returns status code
    Result write(const SharedBuffer& buf, bool flush=true) override;

    /// Writes raw data, returns status code
    Result write(const SerializedMsg& fragments, bool flush=true) override;

    /// Shutdowns write side, waits for pending write requests to complete, but on reactor's side
    void shutdown() override;

private:
    friend class SslServer;
    friend class Reactor;
    friend class TcpConnectors;

    explicit SslStream(const SSLContext::Ptr& ctx);

    bool on_read(ErrorCode ec, void* data, size_t size) override;
    bool on_decrypted_data(void* data, size_t size);
    Result on_encrypted_data(const SharedBuffer& data, bool flush);

    SSLIO _ssl;
};

}} //namespaces

