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
    _ssl(ctx, BIND_THIS_MEMFN(on_decrypted_data), BIND_THIS_MEMFN(on_encrypted_data))
{}

Result SslStream::write(const SharedBuffer& buf, bool flush) {
    _ssl.enqueue(buf);
    return flush ? _ssl.flush_write_buffer() : Ok();
}

Result beam::io::SslStream::write(const SerializedMsg& fragments, bool flush) {
    for (const auto& buf : fragments) {
        _ssl.enqueue(buf);
    }
    return flush ? _ssl.flush_write_buffer() : Ok();
}

void SslStream::on_decrypted_data(io::ErrorCode ec, void* data, size_t size) {
    TcpStream::on_read(ec, data, size);
}

Result SslStream::on_encrypted_data(SerializedMsg& data) {
    return TcpStream::write(data);
}

}} //namespaces
