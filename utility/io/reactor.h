#pragma once
#include "errorhandling.h"
#include "mempool.h"
#include "address.h"
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace beam { namespace io {

class TcpStream;
class CoarseTimer;

class Reactor : public std::enable_shared_from_this<Reactor> {
public:
    Reactor(const Reactor&) = delete;
    Reactor& operator=(const Reactor&) = delete;

    using Ptr = std::shared_ptr<Reactor>;

    /// Creates a new reactor. Throws on errors
    static Ptr create();

    /// Performs shutdown and cleanup.
    virtual ~Reactor();

    /// Runs the reactor. This function blocks.
    void run();

    /// Stops the running reactor.
    /// NOTE: Called from another thread.
    void stop();

    using ConnectCallback = std::function<void(uint64_t tag, std::unique_ptr<TcpStream>&& newStream, ErrorCode errorCode)>;

    Result tcp_connect(Address address, uint64_t tag, const ConnectCallback& callback, int timeoutMsec=-1, Address bindTo=Address());

    void cancel_tcp_connect(uint64_t tag);

	class Scope
	{
		Reactor* m_pPrev;
	public:
		Scope(Reactor&);
		~Scope();
	};

	static Reactor& get_Current();

private:
    /// Ctor. private and called by create()
    Reactor();

    // called by create()returns error code
    ErrorCode initialize();

    /// Pollable objects' base
    struct Object {
        Object() = default;
        Object(const Object&) = delete;
        Object& operator=(const Object&) = delete;

        Object(Object&& o) :
            _reactor(std::move(o._reactor)),
            _handle(o._handle)
        {
            o._handle = 0;
        }

        Object& operator=(Object&& o) {
            _reactor = std::move(o._reactor);
            _handle = o._handle;
            o._handle = 0;
            return *this;
        }

        ~Object() {
            async_close();
        }

        void async_close() {
            if (_handle) {
                if (_reactor) {
                    _reactor->async_close(_handle);
                    _reactor.reset();
                }
                else if (_handle->loop->data) {
                    // object owned by Reactor itself
                    reinterpret_cast<Reactor*>(_handle->loop->data)->async_close(_handle);
                }
            }
        }

        Reactor::Ptr _reactor;
        uv_handle_t* _handle=0;
    };

    struct ConnectContext {
        uint64_t tag;
        ConnectCallback callback;
        uv_connect_t* request;
    };

    void connect_callback(ConnectContext* ctx, ErrorCode errorCode);

    void connect_timeout_callback(uint64_t tag);

    void cancel_tcp_connect_impl(std::unordered_map<uint64_t, ConnectContext>::iterator& it);

    ErrorCode init_asyncevent(Object* o, uv_async_cb cb);

    ErrorCode init_timer(Object* o);
    ErrorCode start_timer(Object* o, unsigned intervalMsec, bool isPeriodic, uv_timer_cb cb);
    void cancel_timer(Object* o);

    ErrorCode init_tcpserver(Object* o, Address bindAddress, uv_connection_cb cb);
    ErrorCode init_tcpstream(Object* o);
    ErrorCode accept_tcpstream(Object* acceptor, Object* newConnection);
    void shutdown_tcpstream(Object* o);

    ErrorCode init_object(ErrorCode errorCode, Object* o, uv_handle_t* h);
    void async_close(uv_handle_t*& handle);

    struct WriteRequest {
        uv_write_t req;
        size_t n;
    };

    WriteRequest* alloc_write_request();
    void release_write_request(WriteRequest*& req);

    union Handles {
        uv_timer_t timer;
        uv_async_t async;
        uv_tcp_t tcp;
    };

    uv_loop_t _loop;
    uv_async_t _stopEvent;
    MemPool<uv_handle_t, sizeof(Handles)> _handlePool;
    MemPool<uv_connect_t, sizeof(uv_connect_t)> _connectRequestsPool;
    MemPool<WriteRequest, sizeof(WriteRequest)> _writeRequestsPool;
    MemPool<uv_shutdown_t, sizeof(uv_shutdown_t)> _shutdownRequestsPool;
    std::unordered_map<uint64_t, ConnectContext> _connectRequests;
    std::unordered_set<uv_shutdown_t*> _shutdownRequests;
    std::unordered_set<uv_connect_t*> _cancelledConnectRequests;
    std::unique_ptr<CoarseTimer> _connectTimer;
    bool _creatingInternalObjects=false;

    friend class AsyncEvent;
    friend class Timer;
    friend class TcpServer;
    friend class TcpStream;
};

}} //namespaces
