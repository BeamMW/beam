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

#include "sslstream.h"
#include "utility/helpers.h"

namespace beam { namespace io {

SslStream::SslStream(const SSLContext::Ptr& ctx) :
    _ssl(ctx, BIND_THIS_MEMFN(on_decrypted_data), BIND_THIS_MEMFN(on_encrypted_data), 16384)
{}

Result SslStream::write(const SharedBuffer& buf, bool flush) {
    _ssl.enqueue(buf);
    return flush ? _ssl.flush() : Ok();
}

Result SslStream::write(const SerializedMsg& fragments, bool flush) {
    for (const auto& buf : fragments) {
        _ssl.enqueue(buf);
    }
    return flush ? _ssl.flush() : Ok();
}

void SslStream::shutdown() {
    //_ssl.flush();
    _ssl.shutdown();
    TcpStream::shutdown();
}

bool SslStream::on_read(ErrorCode ec, void *data, size_t size) {
    if (ec == EC_OK) {
        Result res = _ssl.on_encrypted_data_from_stream(data, size);
        if (!res) {
            auto errorCode = res.error();
            if (errorCode != EC_ENOTCONN) {
                return TcpStream::on_read(errorCode, 0, 0);
            } else {
                return false;
            }
        }
    } else {
        return TcpStream::on_read(ec, 0, 0);
    }
    return true;
}

bool SslStream::on_decrypted_data(void* data, size_t size) {
    return TcpStream::on_read(EC_OK, data, size);
}

Result SslStream::on_encrypted_data(const SharedBuffer& data, bool flush) {
    return TcpStream::write(data, flush);
}

}} //namespaces
