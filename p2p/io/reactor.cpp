#include "reactor.h"
#include "exception.h"
#include "tcpstream.h"
#include <assert.h>
#include <stdlib.h>

namespace io {

Reactor::Ptr Reactor::create(const Config& config) {
    return Reactor::Ptr(new Reactor(config));
}

Reactor::Reactor(const Config& config) :
    _config(config),
    _handlePool(config.handle_pool_size)
{
    int r = uv_loop_init(&_loop);
    if (r != 0) IO_EXCEPTION(r, "cannot initialize uv loop");

    _loop.data = this;

    r = uv_async_init(&_loop, &_stopEvent, [](uv_async_t* handle) { uv_stop(handle->loop); });
    if (r != 0) {
        uv_loop_close(&_loop);
        IO_EXCEPTION(r, "cannot initialize loop stop event");
    }
}

Reactor::~Reactor() {
    uv_close((uv_handle_t*)&_stopEvent, 0);
    
    if (!_connectRequests.empty()) {
        for (auto& c : _connectRequests) {
            uv_handle_t* h = (uv_handle_t*)c.first;
            async_close(h);
        }
    }
    
    // run one cycle to release all closing handles
    uv_run(&_loop, UV_RUN_NOWAIT);

    if (uv_loop_close(&_loop) == UV_EBUSY) {
        //TODO log
        uv_walk(
            &_loop,
            [](uv_handle_t* handle, void*) {
                if (!uv_is_closing(handle)) {
                    handle->data = 0;
                    uv_close(handle, 0);
                }
            },
            0
        );

        // once more
        uv_run(&_loop, UV_RUN_NOWAIT);
        uv_loop_close(&_loop);
    }
}

void Reactor::run() {
    // NOTE: blocks
    uv_run(&_loop, UV_RUN_DEFAULT);
}

void Reactor::stop() {
    int r = uv_async_send(&_stopEvent);
    if (r != 0) {
        // TODO log
    }
}

bool Reactor::init_object(int status, Reactor::Object* o, uv_handle_t* h) {
    _lastError = status;
    if (status != 0) {
        _handlePool.release(h);
        return false;
    }
    h->data = o;
    o->_reactor = shared_from_this();
    o->_handle = h;
    return true;
}

bool Reactor::init_asyncevent(Reactor::Object* o, uv_async_cb cb) {
    assert(o);
    assert(cb);
    
    uv_handle_t* h = _handlePool.alloc();
    int status = uv_async_init(
        &_loop,
        (uv_async_t*)h,
        cb
    );
    return init_object(status, o, h);
}

bool Reactor::init_timer(Reactor::Object* o) {
    assert(o);
    uv_handle_t* h = _handlePool.alloc();
    int status = uv_timer_init(&_loop, (uv_timer_t*)h);
    return init_object(status, o, h);
}

bool Reactor::start_timer(Reactor::Object* o, unsigned intervalMsec, bool isPeriodic, uv_timer_cb cb) {
    assert(o);
    assert(cb);
    assert(o->_handle && o->_handle->type == UV_TIMER);
    
    _lastError = uv_timer_start(
        (uv_timer_t*)o->_handle,
        cb,
        intervalMsec,
        isPeriodic ? intervalMsec : 0
    );
    
    return (_lastError == 0);
}

void Reactor::cancel_timer(Object* o) {
    assert(o);
    
    uv_handle_t* h = o->_handle;
    if (h) {
        assert(h->type == UV_TIMER);
        if (uv_is_active(h)) {
            int r = uv_timer_stop((uv_timer_t*)h);
            if (r != 0) {
                //TODO log
            }
        }
    }
}

bool Reactor::init_tcpserver(Object* o, Address bindAddress, uv_connection_cb cb) {
    assert(o);
    assert(cb);

    uv_handle_t* h = _handlePool.alloc();
    int status = uv_tcp_init(&_loop, (uv_tcp_t*)h);
    if (!init_object(status, o, h)) {
        return false;
    }
        
    sockaddr_in addr;
    bindAddress.fill_sockaddr_in(addr);
    
    _lastError = uv_tcp_bind((uv_tcp_t*)h, (const sockaddr*)&addr, 0);
    if (_lastError) {
        return false;
    }

    _lastError = uv_listen(
        (uv_stream_t*)h,
        _config.tcp_listen_backlog,
        cb
    );

    return (_lastError == 0);
}

bool Reactor::init_tcpstream(Object* o) {
    assert(o);
    uv_handle_t* h = _handlePool.alloc();
    int status = uv_tcp_init(&_loop, (uv_tcp_t*)h);
    return init_object(status, o, h);
}

int Reactor::accept_tcpstream(Object* acceptor, Object* newConnection) {
    assert(acceptor->_handle);
            
    if (!init_tcpstream(newConnection)) {
        return _lastError;
    }
        
    int status = uv_accept((uv_stream_t*)acceptor->_handle, (uv_stream_t*)newConnection->_handle);
    if (status != 0) {
        newConnection->async_close();
    }

    return status;
}

bool Reactor::tcp_connect(Address address, uint64_t tag, ConnectCallback&& callback) {
    assert(callback);
    
    if (!address || _connectRequests.count(tag) > 0) {
        _lastError = UV_EINVAL;
        return false;
    }
    
    uv_handle_t* h = _handlePool.alloc();
    _lastError = uv_tcp_init(&_loop, (uv_tcp_t*)h);
    if (_lastError) {
        _handlePool.release(h);
        return false;
    }
    h->data = this;
    
    ConnectContext ctx;
    ctx.tag = tag;
    ctx.callback = std::move(callback);
    ctx.request.data = &ctx;
    
    sockaddr_in addr;
    address.fill_sockaddr_in(addr);
    
    _lastError = uv_tcp_connect(
        &(ctx.request),
        (uv_tcp_t*)h,
        (const sockaddr*)&addr,
        [](uv_connect_t* request, int status) {
            if (status == UV_ECANCELED) {
                return;
            }
            assert(request);
            assert(request->data);
            assert(request->handle);
            assert(request->handle->loop);
            assert(request->handle->loop->data);
            ConnectContext* ctx = reinterpret_cast<ConnectContext*>(request->data);
            Reactor* reactor = reinterpret_cast<Reactor*>(request->handle->loop->data);
            reactor->connect_callback(ctx, status);
        }
    );
    if (_lastError) {
        async_close(h);
        return false;
    }

    _connectRequests[tag] = std::move(ctx);
    return true;
}

void Reactor::connect_callback(Reactor::ConnectContext* ctx, int status) {
    assert(_connectRequests.count(ctx->tag)==1);

    uint64_t tag = ctx->tag;
    ConnectCallback callback = std::move(ctx->callback);
    uv_handle_t* h = (uv_handle_t*)ctx->request.handle;
    
    TcpStream::Ptr stream;
    if (status == 0) {
        stream.reset(new TcpStream());
        stream->_handle = h;
        stream->_handle->data = stream.get();
        stream->_reactor = shared_from_this();
    } else {
        async_close(h);
    }
    
    _connectRequests.erase(tag);
    
    callback(tag, std::move(stream), status);
}

void Reactor::async_close(uv_handle_t*& handle) {
    if (handle && !uv_is_closing(handle)) {
        handle->data = 0;

        uv_close(
            handle,
            [](uv_handle_s* handle) {
                assert(handle->loop);
                Reactor* reactor = reinterpret_cast<Reactor*>(handle->loop->data);
                assert(reactor);
                reactor->_handlePool.release(handle);
            }
        );
    }

    handle = 0;
}

} //namespace
