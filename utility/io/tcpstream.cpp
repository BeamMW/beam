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

#include "tcpstream.h"
#include "utility/config.h"
#include "utility/helpers.h"
#include <assert.h>

#define LOG_DEBUG_ENABLED 0
#include "utility/logger.h"

namespace beam { namespace io {

TcpStream::TcpStream() :
    _onDataWritten(BIND_THIS_MEMFN(on_data_written))
{}

TcpStream::~TcpStream() {
    disable_read();
    if (_handle) _handle->data = 0;
}

void TcpStream::alloc_read_buffer() {
    if (!_readBuffer.base) {
        if (_readBuffer.len == 0) {
            _readBuffer.len = config().get_int("io.stream_read_buffer_size", 256*1024, 2048, 1024*1024*16);
        }
        _readBuffer.base = (char*)malloc(_readBuffer.len);
    }
}

void TcpStream::free_read_buffer() {
    if (_readBuffer.base) free(_readBuffer.base);
    _readBuffer.base = 0;
    _readBuffer.len = 0;
}

Result TcpStream::enable_read(const TcpStream::Callback& callback) {
    assert(callback);

    if (!is_connected()) {
        return make_unexpected(EC_ENOTCONN);
    }

    alloc_read_buffer();

    static uv_alloc_cb read_alloc_cb = [](
        uv_handle_t* handle,
        size_t /*suggested_size*/,
        uv_buf_t* buf
    ) {
        TcpStream* self = reinterpret_cast<TcpStream*>(handle->data);
        if (self) {
            *buf = self->_readBuffer;
        }
    };

    ErrorCode errorCode = (ErrorCode)uv_read_start((uv_stream_t*)_handle, read_alloc_cb, read_cb);
    if (errorCode != 0) {
        _callback = Callback();
        free_read_buffer();
        return make_unexpected(errorCode);
    }

    _callback = callback;
    return Ok();
}

void TcpStream::disable_read() {
    _callback = Callback();
    if (is_connected()) {
        int errorCode = uv_read_stop((uv_stream_t*)_handle);
        if (errorCode) {
            LOG_DEBUG() << "uv_read_stop failed,code=" << errorCode;
        }
    }
    free_read_buffer();
}

Result TcpStream::write(const SharedBuffer& buf, bool flush) {
    if (!is_connected()) return make_unexpected(EC_ENOTCONN);
    _writeBuffer.append(buf);
    return do_write(flush);
}

Result TcpStream::write(const SerializedMsg& fragments, bool flush) {
    if (!is_connected()) return make_unexpected(EC_ENOTCONN);
    if (!fragments.empty()) {
        for (const auto& f : fragments) {
            _writeBuffer.append(f);
        }
    }
    return do_write(flush);
}

/*
Result TcpStream::write(const BufferChain& fragments, bool flush) {
    if (!is_connected()) return make_unexpected(EC_ENOTCONN);
    _writeBuffer.append(fragments);
    return do_write(flush);
}
*/
void TcpStream::shutdown() {
    if (is_connected()) {
        disable_read();
        do_write(true);
        _reactor->shutdown_tcpstream(this);
        assert(!_callback);
        assert(!is_connected());
    }
}

void TcpStream::enable_keepalive(unsigned initialDelaySecs) {
    if (_handle) {
        uv_tcp_keepalive((uv_tcp_t*)_handle, 1, initialDelaySecs);
    }
}

Result TcpStream::do_write(bool flush) {
    size_t nBytes = _writeBuffer.size();
    if (flush && nBytes > 0) {
        ErrorCode ec = _reactor->async_write(this, _writeBuffer, _onDataWritten);
        if (ec != EC_OK) {
            LOG_DEBUG() << __FUNCTION__ << " " << error_str(ec);
            return make_unexpected(ec);
        }
        _state.unsent += nBytes;
    }
    if (flush) assert(_writeBuffer.empty());
    return Ok();
}

void TcpStream::on_data_written(ErrorCode errorCode, size_t n) {
    if (errorCode != EC_OK) {
        if (_callback) _callback(errorCode, 0, 0);
    } else {
        _state.sent += n;
        assert(_state.unsent >= n);
        _state.unsent -= n;
    }
    LOG_DEBUG() << __FUNCTION__ << TRACE(n) << TRACE(_state.unsent) << TRACE(_state.sent) << TRACE(_state.received);
}

bool TcpStream::is_connected() const {
    return _handle != 0;
}

Address TcpStream::address() const {
    if (!is_connected()) return Address();
    sockaddr_in sa;
    int size = sizeof(sockaddr_in);
    uv_tcp_getsockname((const uv_tcp_t*)_handle, (sockaddr*)&sa, &size);
    return Address(sa);
}

Address TcpStream::peer_address() const {
    if (!is_connected()) return Address();
    sockaddr_in sa;
    int size = sizeof(sockaddr_in);
    uv_tcp_getpeername((const uv_tcp_t*)_handle, (sockaddr*)&sa, &size);
    return Address(sa);
}

void TcpStream::read_cb(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf) {
    TcpStream* self = reinterpret_cast<TcpStream*>(handle->data);

    // self becomes null after async close
    if (self) {
        if (nread > 0) self->on_read(EC_OK, buf->base, size_t(nread));
        else if (nread < 0) self->on_read(ErrorCode(nread), 0, 0);
    }
}

bool TcpStream::on_read(ErrorCode errorCode, void* data, size_t size) {
    if (_callback) {
        _state.received += size;
        LOG_DEBUG() << __FUNCTION__ << TRACE(size) << TRACE(_state.unsent) << TRACE(_state.sent) << TRACE(_state.received);
        return _callback(errorCode, data, size);
    }
    return false;
}

}} //namespaces

