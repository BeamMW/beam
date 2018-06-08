#include "tcpstream.h"
#include "utility/config.h"
#include <assert.h>

#define LOG_VERBOSE_ENABLED 1
#include "utility/logger.h"

namespace beam { namespace io {

TcpStream::~TcpStream() {
    disable_read();
    LOG_VERBOSE() << ".";
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

    ErrorCode errorCode = (ErrorCode)uv_read_start((uv_stream_t*)_handle, read_alloc_cb, on_read);
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
    free_read_buffer();
    if (is_connected()) {
        int errorCode = uv_read_stop((uv_stream_t*)_handle);
        if (errorCode) {
            LOG_DEBUG() << "uv_read_stop failed,code=" << errorCode;
        }
    }
}

Result TcpStream::write(const SharedBuffer& buf) {
    if (!buf.empty()) {
        if (!is_connected()) return make_unexpected(EC_ENOTCONN);
        _writeBuffer.append(buf);
        _state.unsent = _writeBuffer.size();
        return send_write_request();
    }
    return Ok();
}

Result TcpStream::write(const std::vector<SharedBuffer>& fragments) {
    size_t n = fragments.size();
    if (n != 0) {
        if (n == 1) return write(fragments[0]);
        if (!is_connected()) return make_unexpected(EC_ENOTCONN);
        for (const auto& f : fragments) {
            _writeBuffer.append(f);
        }
        _state.unsent = _writeBuffer.size();
        return send_write_request();
    }
    return Ok();
}

void TcpStream::shutdown() {
    if (is_connected()) {
        disable_read();
        _reactor->shutdown_tcpstream(this);
        assert(!_callback);
        assert(!is_connected());
    }
}

Result TcpStream::send_write_request() {
    static uv_write_cb write_cb = [](uv_write_t* req, int errorCode) {
        LOG_VERBOSE() << TRACE(errorCode);

        Reactor::WriteRequest* wr = reinterpret_cast<Reactor::WriteRequest*>(req);
        if (errorCode != UV_ECANCELED) {
            // object may be no longer alive if UV_CANCELED

            assert(wr->req.handle);

            TcpStream* self = reinterpret_cast<TcpStream*>(wr->req.handle->data);
            if (self) {
                self->on_data_written(ErrorCode(errorCode), wr->n);
            }
        }
        assert(req->data);
        Reactor* reactor = reinterpret_cast<Reactor*>(req->data);
        reactor->release_write_request(wr);
    };

    if (!_writeRequestSent) {
        Reactor::WriteRequest* wr = _reactor->alloc_write_request();
        wr->n = _writeBuffer.size();
        ErrorCode errorCode = (ErrorCode)uv_write((uv_write_t*)wr, (uv_stream_t*)_handle,
            (uv_buf_t*)_writeBuffer.fragments(), _writeBuffer.num_fragments(), write_cb
        );

        _state.unsent += wr->n;

        if (errorCode != 0) {
            return make_unexpected(errorCode);
        }
        _writeRequestSent = true;
    }

    return Ok();
}

void TcpStream::on_data_written(ErrorCode errorCode, size_t n) {
    LOG_VERBOSE() << TRACE(_handle) << TRACE(errorCode) << TRACE(n) << TRACE(_state.unsent) << TRACE(_state.sent) << TRACE(_state.received);

    if (errorCode != 0) {
        if (_callback) _callback(errorCode, 0, 0);
    } else {
        _writeBuffer.advance(n);
        _state.sent += n;
        assert(_state.unsent >= n);
        _state.unsent -= n;
        _writeRequestSent = false;
        if (!_writeBuffer.empty()) {
            send_write_request();
        }
    }
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

void TcpStream::on_read(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf) {
    LOG_VERBOSE() << TRACE(handle) << TRACE(nread);

    TcpStream* self = reinterpret_cast<TcpStream*>(handle->data);

    // self becomes null after async close

    if (self && self->_callback) {
        if (nread > 0) {
            self->_state.received += nread;
            self->_callback(EC_OK, buf->base, (size_t)nread);
        } else if (nread < 0) {
            self->_callback((ErrorCode)nread, 0, 0);
        }
    }
}

}} //namespaces

