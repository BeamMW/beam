#include "tcpserver.h"
#include <assert.h>

namespace beam { namespace io {

TcpServer::Ptr TcpServer::create(const Reactor::Ptr& reactor, Address bindAddress, Callback&& callback) {
    assert(reactor);
    assert(callback);

    if (!reactor || !callback)
        IO_EXCEPTION(EC_EINVAL);

    Ptr server(new TcpServer(std::move(callback)));
    ErrorCode errorCode = reactor->init_tcpserver(
        server.get(),
        bindAddress,
        [](uv_stream_t* handle, int errorCode) {
            assert(handle);
            TcpServer* s = reinterpret_cast<TcpServer*>(handle->data);
            if (s) s->on_accept(ErrorCode(errorCode));
        }
    );
    IO_EXCEPTION_IF(errorCode);
    return server;
}

TcpServer::TcpServer(Callback&& callback) :
    _callback(std::move(callback))
{}

void TcpServer::on_accept(ErrorCode errorCode) {
    if (errorCode != 0) {
        _callback(TcpStream::Ptr(), errorCode);
        return;
    }
    TcpStream::Ptr stream(new TcpStream());
    errorCode = _reactor->accept_tcpstream(this, stream.get());
    _callback(std::move(stream), errorCode);
}

}} //namespaces

