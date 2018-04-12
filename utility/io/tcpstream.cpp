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

size_t TcpStream::try_write(const void* data, size_t size) {
    if (!is_connected() || !_writeBuffer.empty()) return 0;
    IOVec buf(data, size);
    return try_write(&buf);
}

size_t TcpStream::try_write(const IOVec* buf) {
    int result = uv_try_write((uv_stream_t*)_handle, (uv_buf_t*)buf, 1);
    if (result <= 0){
        return 0; //TODO this is a stub append disconnect events
    }
    _state.sent += result;
    return (size_t) result;
}

bool TcpStream::write(const SharedBuffer& buf) {
    if (buf.empty()) return true;
    if (!is_connected()) return false;
    if (_writeBuffer.empty()) {
        size_t sent = try_write(&buf);
        if (sent == 0) {
            return false;
        }
        _state.sent += sent;
        if (sent == buf.size) {
            return true;
        } else {
            SharedBuffer b(buf);
            b.advance(sent);
            _writeBuffer.append(b);
        }
    } else {
        _writeBuffer.append(buf);
    }
    _state.unsent = _writeBuffer.size();
    return send_write_request();
}

bool TcpStream::send_write_request() {
    static uv_write_cb write_cb = [](uv_write_t* req, int status) {
        if (status == UV_ECANCELED) {
            // object may be no longer alive ???
            return;
        }
        TcpStream* self = reinterpret_cast<TcpStream*>(req->handle->data);
        assert(self);
        assert(&(self->_writeRequest) == req);
        if (status != 0) {
            if (self->_callback) self->_callback(status, 0, 0);
        } else {
            size_t sent = self->_state.unsent;
            self->_writeBuffer.advance(sent);
            self->_state.sent += sent;
            self->_state.unsent = self->_writeBuffer.size();
            self->_writeRequestSent = false;
            if (self->_state.unsent) {
                self->send_write_request();
            }
        }
    };

    if (_writeRequestSent) return true;

    int r = uv_write(&_writeRequest, (uv_stream_t*)_handle,
        (uv_buf_t*)_writeBuffer.fragments(), _writeBuffer.num_fragments(), write_cb
    );

    if (r != 0) {
        // TODO close handle ??
        _lastError = r;
        return false;
    }
    _writeRequestSent = true;
    return true;
}

bool TcpStream::is_connected() const {
    return _handle != 0;
}

}} //namespaces

