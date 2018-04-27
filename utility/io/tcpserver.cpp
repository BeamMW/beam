#include "tcpserver.h"
#include <assert.h>

namespace beam { namespace io {

TcpServer::Ptr TcpServer::create(const Reactor::Ptr& reactor, Address bindAddress, Callback&& callback) {
    assert(reactor);
    assert(callback);

    Ptr server(new TcpServer(std::move(callback)));
    if (reactor->init_tcpserver(
        server.get(),
        bindAddress,
        [](uv_stream_t* handle, int status) {
            assert(handle);
            assert(handle->data);
            reinterpret_cast<TcpServer*>(handle->data)->internal_callback(status);
        }
    )) {
        return server;
    }
    return Ptr();
}


TcpServer::TcpServer(Callback&& callback) :
    _callback(std::move(callback))
{}

void TcpServer::internal_callback(int status) {
    if (status != 0) {
        _callback(TcpStream::Ptr(), status);
        return;
    }
    TcpStream::Ptr stream(new TcpStream());
    status = _reactor->accept_tcpstream(this, stream.get());
    _callback(std::move(stream), status);
}

}} //namespaces

