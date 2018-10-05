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

#include "sslserver.h"
#include "sslstream.h"
#include <assert.h>

namespace beam { namespace io {

TcpServer::Ptr SslServer::create(
    Reactor& reactor, Address bindAddress, Callback&& callback,
    const char* certFileName, const char* privKeyFileName
) {
    assert(callback && certFileName && privKeyFileName);

    if (!callback || !certFileName || !privKeyFileName)
        IO_EXCEPTION(EC_EINVAL);

    SSLContext::Ptr ctx = SSLContext::create_server_ctx(certFileName, privKeyFileName);

    return Ptr(new SslServer(std::move(callback), reactor, bindAddress, std::move(ctx)));
}

SslServer::SslServer(Callback&& callback, Reactor& reactor, Address bindAddress, SSLContext::Ptr&& ctx) :
    TcpServer(std::move(callback), reactor, bindAddress),
    _ctx(std::move(ctx))
{}

void SslServer::on_accept(ErrorCode errorCode) {
    if (errorCode != EC_OK) {
        _callback(TcpStream::Ptr(), errorCode);
        return;
    }

    TcpStream::Ptr stream;
    try {
        stream.reset(new SslStream(_ctx));
    } catch (...) {
        _callback(TcpStream::Ptr(), EC_SSL_ERROR);
        return;
    }

    errorCode = _reactor->accept_tcpstream(this, stream.get());
    _callback(std::move(stream), errorCode);
}

}} //namespaces

