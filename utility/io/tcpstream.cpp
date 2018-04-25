#include "tcpstream.h"
#include <assert.h>

namespace beam { namespace io {

bool TcpStream::enable_read(const TcpStream::Callback& callback) {
    assert(callback);

    if (!is_connected()) {
        _lastError = UV_ENOTCONN;
        return false;
    }

    _readBuffer.resize(_reactor->config().stream_read_buffer_size);

    static uv_alloc_cb read_alloc_cb = [](uv_handle_t* handle, size_t /*suggested_size*/, uv_buf_t* buf) {
        TcpStream* self = reinterpret_cast<TcpStream*>(handle->data);
        assert(self);
        buf->base = self->_readBuffer.data();
        buf->len = self->_readBuffer.size();
    };

    static uv_read_cb read_cb = [](uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf) {
        TcpStream* self = reinterpret_cast<TcpStream*>(handle->data);
        assert(self);
        if (self->_callback) {
            if (nread > 0) {
                assert(buf->base == self->_readBuffer.data());
                assert(buf->len <= self->_readBuffer.size());
                assert(buf->len >= (size_t)nread);
                self->_state.received += nread;
                self->_callback(0, buf->base, (size_t)nread);
            } else if (nread < 0) {
                self->_callback((int)nread, 0, 0);
            }
        }
    };

    _lastError = uv_read_start((uv_stream_t*)_handle, read_alloc_cb, read_cb);
    if (_lastError != 0) {
        _callback = Callback();
        return false;
    }

    _callback = callback;
    return true;
}

bool TcpStream::disable_read() {
    _lastError = 0;
    if (!is_connected()) {
        _lastError = UV_ENOTCONN;
        return false;
    }
    if (_callback) {
        _lastError = uv_read_stop((uv_stream_t*)_handle);
        _callback = Callback();
    }
    return (_lastError == 0);
}

int TcpStream::write(const SharedBuffer& buf) {
    if (buf.empty()) return 0;
    if (!is_connected()) return ENOTCONN;
    _writeBuffer.append(buf);
    _state.unsent = _writeBuffer.size();
    return send_write_request();
}

int TcpStream::write(const std::vector<SharedBuffer>& fragments) {
    size_t n = fragments.size();
    if (n == 0) return true;
    if (n == 1) return write(fragments[0]);
    if (!is_connected()) return false;
    for (const auto& f : fragments) {
        _writeBuffer.append(f);
    }
    _state.unsent = _writeBuffer.size();
    return send_write_request();
}

int TcpStream::send_write_request() {
    static uv_write_cb write_cb = [](uv_write_t* req, int status) {
        if (status == UV_ECANCELED) {
            // object may be no longer alive
            return;
        }
        TcpStream* self = reinterpret_cast<TcpStream*>(req->handle->data);
        if (!self) {
            //stream was closed
            return;
        }
        assert(&(self->_writeRequest) == req);
        self->on_data_written(status);
    };

    if (_writeRequestSent) return true;

    int r = uv_write(&_writeRequest, (uv_stream_t*)_handle,
        (uv_buf_t*)_writeBuffer.fragments(), _writeBuffer.num_fragments(), write_cb
    );

    if (r != 0) {
        // TODO close handle ??
        _lastError = r;
        return r;
    }
    _writeRequestSent = true;
    return 0;
}

void TcpStream::on_data_written(int status) {
    if (status != 0) {
        if (_callback) _callback(status, 0, 0);
    } else {
        size_t sent = _state.unsent;
        _writeBuffer.advance(sent);
        _state.sent += sent;
        _state.unsent = _writeBuffer.size();
        _writeRequestSent = false;
        if (_state.unsent) {
            send_write_request();
        }
    }
}

bool TcpStream::is_connected() const {
    return _handle != 0;
}

}} //namespaces

