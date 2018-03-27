#include "tcpserver.h"
#include "exception.h"
#include <assert.h>

namespace io {

// TODO configure
static const int TCP_LISTEN_BACKLOG = 100;

TcpServer::TcpServer(
    Reactor::Ptr reactor,
    uint16_t port,
    Callback&& callback,
    bool listenToLocalhostOnly
) :
    Reactor::Object(reactor),
    _callback(std::move(callback))
{
    // TODO assert that callback is callable

    int r = uv_tcp_init(
        &(_reactor->_loop),
        (uv_tcp_t*)_handle
    );
    if (r != 0) IO_EXCEPTION(r, "cannot initialize tcp server");

    struct sockaddr_in addr;

    // TODO accept explicit interface
    static const char* bindInterface = listenToLocalhostOnly ? "127.0.0.1" : "0.0.0.0";

    uv_ip4_addr(bindInterface, port, &addr);

    r = uv_tcp_bind((uv_tcp_t*)_handle, (const struct sockaddr*)&addr, 0);
    if (r != 0) IO_EXCEPTION(r, (std::string("cannot bind tcp server to ") + bindInterface + ":" + std::to_string(port)).c_str());

    r = uv_listen(
        (uv_stream_t*)_handle,
        TCP_LISTEN_BACKLOG,
        [](uv_stream_t* acceptor, int status) {
            TcpServer* self = reinterpret_cast<TcpServer*>(acceptor->data);
            assert(self);
            self->internal_callback(status);
        }
    );

    if (r != 0) IO_EXCEPTION(r, "cannot listen");
}

void TcpServer::internal_callback(int status) {
    // TODO move stuff into Reactor
    TcpStream stream(_reactor, Config());

    if (status != 0) {
        _callback(std::move(stream), status);
        return;
    }

    status = stream.accepted(_handle);
    _callback(std::move(stream), status);
}

} //namespace

