#include "tcpstream.h"

namespace io {

TcpStream::TcpStream(Reactor::Ptr reactor) :
    Reactor::Object(reactor, true)
{}

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

