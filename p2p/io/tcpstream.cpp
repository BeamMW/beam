#include "tcpstream.h"
#include <assert.h>

namespace io {

TcpStream::TcpStream(Reactor::Ptr reactor, const Config& config) :
    Reactor::Object(reactor, 0)
{
    _readBuffer.resize(config.stream_read_buffer_size);
}

bool TcpStream::enable(TcpStream::Callback&& callback) {
    if (!is_connected()) return false;
    if (!callback) {
        if (_callback) {
            uv_read_stop((uv_stream_t*)_handle);
            _callback = Callback();
        }
        return true;
    }

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
                self->_callback(0, buf->base, (size_t)nread);
            } else if (nread < 0) {
                self->_callback((int)nread, 0, 0);
            }
        }
    };

    int r = uv_read_start((uv_stream_t*)_handle, read_alloc_cb, read_cb);
    if (r != 0) {
        _callback = Callback();
        return false;
    }

    _callback = std::move(callback);
    return true;
}

size_t TcpStream::try_write(const void* data, size_t size) {
    if (!is_connected()) return 0;
    uv_buf_t buf;
    buf.base = (char*)data;
    buf.len = size;
    int result = uv_try_write((uv_stream_t*)_handle, &buf, 1);
    if (result <= 0) return 0; //TODO this is a stub
    return (size_t) result;
}

bool TcpStream::is_connected() {
    return _handle != 0;
}

int TcpStream::accepted(uv_handle_t* acceptor) {
    _handle = _reactor->init_object(this);
    int r = uv_tcp_init(
        &(_reactor->_loop),
        (uv_tcp_t*)_handle
    );
    if (r != 0) {
        _reactor->release(_handle);
        return r;
    }
    r = uv_accept((uv_stream_t*)acceptor, (uv_stream_t*)_handle);
    if (r != 0) {
        async_close();
        return r;
    }
    return 0;
}

} //namespace

