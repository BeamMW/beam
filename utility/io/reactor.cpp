// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "reactor.h"
#include "coarsetimer.h"
#include "sslstream.h"
#include "utility/config.h"
#include "utility/helpers.h"
#include <assert.h>
#include <stdlib.h>

#ifndef WIN32
#include <signal.h>
#endif // WIN32

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif
#include "utility/logger.h"

namespace beam { namespace io {

class TcpConnectors {
public:
    using Callback = Reactor::ConnectCallback;

    explicit TcpConnectors(Reactor& r) :
        _reactor(r),
        _connectRequestsPool(config().get_int("io.connect_pool_size", 16, 0, 512))
    {}

    ~TcpConnectors() {
        if (!_connectRequests.empty()) {
            LOG_ERROR() << "connect requests were not cancelled";
        }
        if (!_cancelledConnectRequests.empty()) {
            LOG_ERROR() << "callbacks on cancelled requests were not called";
        }
    }

    void cancel_all() {
        for (auto& p : _connectRequests) {
            uv_handle_t* h = (uv_handle_t*)(p.second->handle);
            _reactor.async_close(h);
        }
        _connectTimer.reset();
    }

    bool is_tag_free(uint64_t tag) {
        return _connectRequests.count(tag) == 0;
    }

    Result tcp_connect(uv_tcp_t* handle, Address address, uint64_t tag, const Callback& callback, int timeoutMsec, bool isTls) {
        assert(is_tag_free(tag));

        if (timeoutMsec >= 0) {
            if (!_connectTimer) {
                try {
                    _connectTimer = CoarseTimer::create(
                        _reactor,
                        config().get_int("io.connect_timer_resolution", 1000, 1, 60000),
                        BIND_THIS_MEMFN(connect_timeout_callback)
                    );
                } catch (const Exception& e) {
                    return make_unexpected(e.errorCode);
                }
            }
            auto result = _connectTimer->set_timer(timeoutMsec, tag);
            if (!result) return result;
        }

        handle->data = 0;
        ConnectRequest* cr = _connectRequestsPool.alloc();
        cr->data = this;
        cr->tag = tag;
        new(&cr->callback) Callback(callback);
        cr->isTls = isTls;

        _connectRequests[tag] = cr;

        sockaddr_in addr;
        address.fill_sockaddr_in(addr);

        auto errorCode = (ErrorCode)uv_tcp_connect(
            cr,
            handle,
            (const sockaddr*)&addr,
            [](uv_connect_t* request, int errorCode) {
                assert(request);
                assert(request->data);
                TcpConnectors* tcpConnectors = reinterpret_cast<TcpConnectors*>(request->data);
                ConnectRequest* cr = static_cast<ConnectRequest*>(request);
                if (errorCode == UV_ECANCELED) {
                    tcpConnectors->_cancelledConnectRequests.erase(cr);
                } else {
                    tcpConnectors->connect_callback(cr->tag, (uv_handle_t*)request->handle, cr->callback, cr->isTls, (ErrorCode)errorCode);
                }
                cr->callback.~Callback();
                tcpConnectors->_connectRequestsPool.release(cr);
            }
        );

        if (errorCode) {
            _connectRequestsPool.release(cr);
            if (_connectTimer) _connectTimer->cancel(tag);
            _connectRequests.erase(tag);
            return make_unexpected(errorCode);
        }

        return Ok();
    }

    void cancel_tcp_connect(uint64_t tag) {
        LOG_VERBOSE() << TRACE(tag);
        auto it = _connectRequests.find(tag);
        if (it != _connectRequests.end()) {
            cancel_tcp_connect_impl(it);
            if (_connectTimer) _connectTimer->cancel(tag);
        }
    }

    void destroy_connect_timer_if_needed()
    {
        if (_connectRequests.empty()) {
            // we have to break cyclic reference to reactor
            _connectTimer.reset();
        }
    }

private:
    struct ConnectRequest : uv_connect_s {
        uint64_t tag;
        Callback callback;
        bool isTls;
    };

    bool create_ssl_context() {
        if (!_sslContext) {
            try {
                _sslContext = SSLContext::create_client_context();
            } catch (...) {
                return false;
            }
        }
        return true;
    }

    void connect_callback(uint64_t tag, uv_handle_t* h, const Callback& callback, bool isTls, ErrorCode errorCode) {
        if (_connectRequests.count(tag) == 0) {
            _reactor.async_close(h);
            return;
        }

        TcpStream::Ptr stream;
        if (errorCode == 0) {
            TcpStream* streamPtr = 0;
            if (isTls) {
                if (!create_ssl_context()) {
                    errorCode = EC_SSL_ERROR;
                } else {
                    streamPtr = new SslStream(_sslContext);
                }
            } else {
                streamPtr = new TcpStream();
            }
            if (streamPtr) {
                stream.reset(_reactor.stream_connected(streamPtr, h));
            }
        } else {
            _reactor.async_close(h);
        }

        _connectRequests.erase(tag);
        if (_connectTimer) _connectTimer->cancel(tag);

        callback(tag, std::move(stream), errorCode);
    }

    void connect_timeout_callback(uint64_t tag) {
        LOG_VERBOSE() << TRACE(tag);
        auto it = _connectRequests.find(tag);
        if (it != _connectRequests.end()) {
            Reactor::ConnectCallback cb = it->second->callback;
            cancel_tcp_connect_impl(it);
            cb(tag, TcpStream::Ptr(), EC_ETIMEDOUT);
        }
    }

    void cancel_tcp_connect_impl(std::unordered_map<uint64_t, ConnectRequest*>::iterator& it) {
        ConnectRequest* cr = it->second;
        auto* h = (uv_handle_t*)cr->handle;
        _reactor.async_close(h);
        _cancelledConnectRequests.insert(cr);
        _connectRequests.erase(it);
    }

    Reactor& _reactor;
    MemPool<ConnectRequest, sizeof(ConnectRequest)> _connectRequestsPool;
    std::unordered_map<uint64_t, ConnectRequest*> _connectRequests;
    std::unordered_set<ConnectRequest*> _cancelledConnectRequests;
    std::unique_ptr<CoarseTimer> _connectTimer;
    SSLContext::Ptr _sslContext;
};

class TcpShutdowns {
public:
    explicit TcpShutdowns(Reactor& r) :
        _reactor(r),
        _shutdownRequestsPool(config().get_int("io.shutdown_pool_size", 16, 0, 512))
    {}

    void cancel_all() {
        for (uv_shutdown_t* sr : _shutdownRequests) {
            uv_handle_t* h = (uv_handle_t*)(sr->handle);
            _reactor.async_close(h);
        }
    }

    void shutdown_tcpstream(Reactor::Object* o) {
        o->_handle->data = 0;

        uv_shutdown_t* req = _shutdownRequestsPool.alloc();
        req->data = this;

        _shutdownRequests.insert(req);

        uv_shutdown(
            req,
            (uv_stream_t*)o->_handle,
            [](uv_shutdown_t* req, int status) {
                if (status != 0 && status != UV_ECANCELED) {
                    LOG_DEBUG() << "stream shutdown failed, code=" << error_str((ErrorCode)status);
                }
                TcpShutdowns* self = reinterpret_cast<TcpShutdowns*>(req->data);
                if (self) {
                    uv_handle_t* req_handle = (uv_handle_t*)req->handle;
                    self->_reactor.async_close(req_handle);
                    self->_shutdownRequests.erase(req);
                    self->_shutdownRequestsPool.release(req);
                }
            }
        );

        o->_handle = 0;
        o->_reactor.reset();
    }

private:
    Reactor& _reactor;
    MemPool<uv_shutdown_t, sizeof(uv_shutdown_t)> _shutdownRequestsPool;
    std::unordered_set<uv_shutdown_t*> _shutdownRequests;
};

class PendingWrites {
public:
    explicit PendingWrites(Reactor& r) :
        _reactor(r),
        _writeRequestsPool(config().get_int("io.write_pool_size", 256, 0, 65536))
    {}

    void cancel_all() {
        for (uv_write_t* wr : _writeRequests) {
            uv_handle_t* h = (uv_handle_t*)(wr->handle);
            _reactor.async_close(h);
        }
    }

    ErrorCode async_write(Reactor::Object* o, BufferChain& unsent, const Reactor::OnDataWritten& cb) {
        uv_write_t* req = _writeRequestsPool.alloc();

        size_t nBytes = unsent.size();
        auto p = _data.insert({ req, Ctx{ this, std::move(unsent), cb, nBytes } });

        assert (p.second);
        Ctx* ctx = &p.first->second;
        req->data = ctx;

        _writeRequests.insert(req);

        auto ec = (ErrorCode)uv_write(
            req,
            (uv_stream_t*)o->_handle,
            (uv_buf_t*)ctx->unsent.fragments(),
            static_cast<unsigned>(ctx->unsent.num_fragments()),
            [](uv_write_t* req, int errorCode) {
                assert(req);
                assert(req->data);
                Ctx* ctx = reinterpret_cast<Ctx*>(req->data);
                assert(ctx->self);
                if (errorCode != UV_ECANCELED && req->handle != 0 && req->handle->data != 0) {
                    // object may be no longer alive if UV_CANCELED
                    assert(ctx->cb);
                    ctx->cb(ErrorCode(errorCode), errorCode == EC_OK ? ctx->nBytes : 0);
                }
                ctx->self->release_request(req);
            }
        );

        if (ec != EC_OK) {
            release_request(req);
        } else {
            unsent.clear();
        }

        return ec;
    }

private:
    struct Ctx {
        PendingWrites* self = 0;
        BufferChain unsent;
        Reactor::OnDataWritten cb;
        size_t nBytes = 0;
    };

    void release_request(uv_write_t* req) {
        _data.erase(req);
        _writeRequests.erase(req);
        _writeRequestsPool.release(req);
    }

    Reactor& _reactor;
    MemPool<uv_write_t, sizeof(uv_write_t)> _writeRequestsPool;
    std::unordered_set<uv_write_t*> _writeRequests;
    std::unordered_map<uv_write_t*, Ctx> _data;
};

Reactor::Ptr Reactor::create() {
    struct make_shared_enabler : public Reactor {};
    return std::make_shared<make_shared_enabler>();
}

Reactor::Reactor() :
    _handlePool(config().get_int("io.handle_pool_size", 256, 0, 65536))
{
    memset(&_loop,0,sizeof(uv_loop_t));
    memset(&_stopEvent, 0, sizeof(uv_async_t));

    _creatingInternalObjects=true;

    auto errorCode = (ErrorCode)uv_loop_init(&_loop);
    if (errorCode != 0) {
        LOG_ERROR() << "cannot initialize uv loop, error=" << errorCode;
        IO_EXCEPTION(errorCode);
    }

    _loop.data = this;

    errorCode = (ErrorCode)uv_async_init(&_loop, &_stopEvent, [](uv_async_t* handle) {
        auto reactor = reinterpret_cast<Reactor*>(handle->data);
        assert(reactor);
        if (reactor && reactor->_stopCB) {
            reactor->_stopCB();
        }
        uv_stop(handle->loop);
    });

    if (errorCode != 0) {
        uv_loop_close(&_loop);
        LOG_ERROR() << "cannot initialize loop stop event, error=" << errorCode;
        IO_EXCEPTION(errorCode);
    }

    _stopEvent.data = this;
    _pendingWrites  = std::make_unique<PendingWrites>(*this);
    _tcpConnectors  = std::make_unique<TcpConnectors>(*this);
    _tcpShutdowns   = std::make_unique<TcpShutdowns>(*this);
    _creatingInternalObjects = false;
}

Reactor::~Reactor() {
    LOG_DEBUG() << __FUNCTION__;

    if (!_loop.data) {
        LOG_DEBUG() << "loop wasn't initialized";
        return;
    }

	if (_tcpConnectors)
		_tcpConnectors->cancel_all();
	if (_pendingWrites)
		_pendingWrites->cancel_all();
	if (_tcpShutdowns)
		_tcpShutdowns->cancel_all();

    if (_stopEvent.data)
        uv_close((uv_handle_t*)&_stopEvent, 0);

    // run one cycle to release all closing handles
    uv_run(&_loop, UV_RUN_NOWAIT);

    if (uv_loop_close(&_loop) == UV_EBUSY) {
        LOG_DEBUG() << "closing unclosed handles";
        uv_walk(
            &_loop,
            [](uv_handle_t* handle, void*) {
                if (!uv_is_closing(handle)) {
                    LOG_DEBUG() << uv_handle_type_name(uv_handle_get_type(handle)) << " " << handle;
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

void Reactor::run_ex(StopCallback&& scb) {
    _stopCB = std::move(scb);
    run();
}

void Reactor::run() {
    if (!_loop.data) {
        LOG_DEBUG() << "loop wasn't initialized";
        return;
    }
    block_sigpipe();
    // NOTE: blocks
    uv_run(&_loop, UV_RUN_DEFAULT);

    // HACK: it is likely that this is the end of the thread, we have to break cycle reference
    _tcpConnectors->destroy_connect_timer_if_needed();
}

void Reactor::stop() {
    int errorCode = uv_async_send(&_stopEvent);
    if (errorCode != 0) {
        LOG_DEBUG() << "cannot post stop signal to event loop";
    }
}

ErrorCode Reactor::init_object(ErrorCode errorCode, Reactor::Object* o, uv_handle_t* h) {
    if (errorCode != 0) {
        _handlePool.release(h);
        return errorCode;
    }
    h->data = o;
    if (!_creatingInternalObjects) {
        o->_reactor = shared_from_this();
    }
    o->_handle = h;
    return EC_OK;
}

ErrorCode Reactor::init_asyncevent(Reactor::Object* o, uv_async_cb cb) {
    assert(o);
    assert(cb);

    uv_handle_t* h = _handlePool.alloc();
    ErrorCode errorCode = (ErrorCode)uv_async_init(
        &_loop,
        (uv_async_t*)h,
        cb
    );
    return init_object(errorCode, o, h);
}

ErrorCode Reactor::init_timer(Reactor::Object* o) {
    assert(o);

    uv_handle_t* h = _handlePool.alloc();
    ErrorCode errorCode = (ErrorCode)uv_timer_init(&_loop, (uv_timer_t*)h);
    return init_object(errorCode, o, h);
}

ErrorCode Reactor::start_timer(Reactor::Object* o, unsigned intervalMsec, bool isPeriodic, uv_timer_cb cb) {
    assert(o);
    assert(cb);
    assert(o->_handle && o->_handle->type == UV_TIMER);

    return (ErrorCode)uv_timer_start(
        (uv_timer_t*)o->_handle,
        cb,
        intervalMsec,
        isPeriodic ? intervalMsec : 0
    );
}

void Reactor::cancel_timer(Object* o) {
    assert(o);

    uv_handle_t* h = o->_handle;
    if (h) {
        assert(h->type == UV_TIMER);
        if (uv_is_active(h)) {
            if (uv_timer_stop((uv_timer_t*)h) != 0) {
                LOG_DEBUG() << "cannot stop timer";
            }
        }
    }
}

ErrorCode Reactor::init_tcpserver(Object* o, Address bindAddress, uv_connection_cb cb) {
    assert(o);
    assert(cb);

    uv_handle_t* h = _handlePool.alloc();
    ErrorCode errorCode = (ErrorCode)uv_tcp_init(&_loop, (uv_tcp_t*)h);
    if (init_object(errorCode, o, h) != EC_OK) {
        return errorCode;
    }

    sockaddr_in addr;
    bindAddress.fill_sockaddr_in(addr);

    errorCode = (ErrorCode)uv_tcp_bind((uv_tcp_t*)h, (const sockaddr*)&addr, 0);
    if (errorCode != 0) {
        return errorCode;
    }

    errorCode = (ErrorCode)uv_listen(
        (uv_stream_t*)h,
        config().get_int("io.tcp_listen_backlog", 32, 5, 2000),
        cb
    );

    return errorCode;
}

ErrorCode Reactor::init_tcpstream(Object* o) {
    assert(o);

    uv_handle_t* h = _handlePool.alloc();
    ErrorCode errorCode = (ErrorCode)uv_tcp_init(&_loop, (uv_tcp_t*)h);
    return init_object(errorCode, o, h);
}

TcpStream* Reactor::stream_connected(TcpStream* stream, uv_handle_t* h) {
    stream->_handle = h;
    stream->_handle->data = stream;
    stream->_reactor = shared_from_this();
    return stream;
}

ErrorCode Reactor::accept_tcpstream(Object* acceptor, Object* newConnection) {
    assert(acceptor->_handle);

    ErrorCode errorCode = init_tcpstream(newConnection);
    if (errorCode != 0) {
        return errorCode;
    }

    errorCode = (ErrorCode)uv_accept((uv_stream_t*)acceptor->_handle, (uv_stream_t*)newConnection->_handle);
    if (errorCode != 0) {
        newConnection->async_close();
    }

    return errorCode;
}

void Reactor::shutdown_tcpstream(Object* o) {
    assert(o);
    uv_handle_t* h = o->_handle;
    if (!h) {
        // already closed
        return;
    }
    assert(o->_reactor.get() == this);

    _tcpShutdowns->shutdown_tcpstream(o);
}

ErrorCode Reactor::async_write(Reactor::Object* o, BufferChain& unsent, const Reactor::OnDataWritten& cb) {
    return _pendingWrites->async_write(o, unsent, cb);
}

Result Reactor::tcp_connect(
    Address address,
    uint64_t tag,
    const ConnectCallback& callback,
    int timeoutMsec,
    bool tlsConnect,
    Address bindTo
) {
    assert(callback);
    assert(!address.empty());
    assert(_tcpConnectors->is_tag_free(tag));

    if (!callback || address.empty() || !_tcpConnectors->is_tag_free(tag)) {
        return make_unexpected(EC_EINVAL);
    }

    uv_handle_t* h = _handlePool.alloc();
    ErrorCode errorCode = (ErrorCode)uv_tcp_init(&_loop, (uv_tcp_t*)h);
    if (errorCode != 0) {
        _handlePool.release(h);
        return make_unexpected(errorCode);
    }

    if (!bindTo.empty()) {
        sockaddr_in bindAddr;
        bindTo.fill_sockaddr_in(bindAddr);

        errorCode = (ErrorCode)uv_tcp_bind((uv_tcp_t*)h, (const sockaddr*)&bindAddr, 0);
        if (errorCode != 0) {
            async_close(h);
            return make_unexpected(errorCode);
        }
    }

    Result res = _tcpConnectors->tcp_connect((uv_tcp_t*)h, address, tag, callback, timeoutMsec, tlsConnect);

    if (!res) {
        async_close(h);
    }

    return res;
}

void Reactor::cancel_tcp_connect(uint64_t tag) {
    _tcpConnectors->cancel_tcp_connect(tag);
}

void Reactor::async_close(uv_handle_t*& handle) {
    LOG_VERBOSE() << "async_close " << TRACE(handle);

    if (!handle) return;
    handle->data = 0;

    if (!uv_is_closing(handle)) {
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

static thread_local Reactor* s_pReactor = NULL;

Reactor::Scope::Scope(Reactor& r)
{
	m_pPrev = s_pReactor;
	s_pReactor = &r;
}

Reactor::Scope::~Scope()
{
	s_pReactor = m_pPrev;
}

Reactor& Reactor::get_Current()
{
	assert(s_pReactor); // core meltdown?
	return *s_pReactor;
}

Reactor* Reactor::GracefulIntHandler::s_pAppReactor = NULL;

Reactor::GracefulIntHandler::GracefulIntHandler(Reactor& r)
{
	assert(!s_pAppReactor);
	s_pAppReactor = &r;

#ifdef WIN32
	SetConsoleCtrlHandler(Handler, TRUE);
#else // WIN32
	SetHandler(true);
#endif // WIN32
}

Reactor::GracefulIntHandler::~GracefulIntHandler()
{
#ifdef WIN32
	SetConsoleCtrlHandler(Handler, FALSE);
#else // WIN32
	SetHandler(false);
#endif // WIN32

	s_pAppReactor = NULL;
}

#ifdef WIN32

BOOL WINAPI Reactor::GracefulIntHandler::Handler(DWORD dwCtrlType)
{
	assert(s_pAppReactor);
	s_pAppReactor->stop();

	return TRUE;
}

#else // WIN32

void Reactor::GracefulIntHandler::SetHandler(bool bSet)
{
	struct sigaction sa;

	sa.sa_handler = bSet ? Handler : NULL;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL);
}

void Reactor::GracefulIntHandler::Handler(int sig)
{
	if (sig != SIGPIPE /*&& sig != SIGHUP*/) {
        assert(s_pAppReactor);
        s_pAppReactor->stop();
    }
}

#endif // WIN32

}} //namespaces
