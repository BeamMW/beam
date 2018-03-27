#include "tcpconnector.h"
#include "exception.h"
#include "config.h"
#include <assert.h>

namespace io {

TcpConnector::TcpConnector(Reactor::Ptr reactor) :
    _reactor(reactor)
{}

TcpConnector::~TcpConnector() {
    if (!_connections.empty()) {
        for (auto& c : _connections) {
            uv_handle_t* h = (uv_handle_t*)c.first;
            _reactor->async_close(h);
        }
    }
}

TcpConnector::ID TcpConnector::connect(std::string ip, uint16_t port, Callback&& callback) {
    struct sockaddr_in addr;
    int r = uv_ip4_addr(ip.c_str(), port, &addr);
    if (r != 0) IO_EXCEPTION(r, (std::string("invalid ip ") + ip).c_str());

    uv_handle_t* h = _reactor->new_handle();
    h->data = this;

    r = uv_tcp_init(
        &(_reactor->_loop),
        (uv_tcp_t*)h
    );
    if (r != 0) {
        _reactor->release(h);
        IO_EXCEPTION(r, "cannot initialize tcp handle");
    }

    Context ctx;
    ctx.callback = std::move(callback);
    ctx.request.data = this;

    r = uv_tcp_connect(
        &(ctx.request),
        (uv_tcp_t*)h,
        (const sockaddr*)&addr,
        [](uv_connect_t* request, int status) {
            if (status == UV_ECANCELED) {
                return;
            }
            TcpConnector* connector = reinterpret_cast<TcpConnector*>(request->data);
            assert(connector);
            connector->internal_callback(request->handle, status);
        }
    );
    if (r != 0) {
        _reactor->async_close(h);
        IO_EXCEPTION(r, "connect request failed");
    }

    _connections[h] = ctx;

    return h;
}

void TcpConnector::cancel(TcpConnector::ID id) {
    auto it = _connections.find(id);
    if (it != _connections.end()) {
        uv_handle_t* h = (uv_handle_t*)id;
        _reactor->async_close(h);
        _connections.erase(id);
    }
}

void TcpConnector::internal_callback(uv_stream_t* handle, int status) {
    auto it = _connections.find(handle);
    assert(it != _connections.end());

    // TODO move this all stuff into Reactor
    TcpStream stream(_reactor, Config());
    Callback callback = std::move(it->second.callback);
    _connections.erase(it);

    if (status == 0) {
        stream.connected(handle);
    }

    callback(handle, std::move(stream), status);
}

} //namespace

