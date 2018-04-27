#pragma once
#include "libuv.h"
#include "mempool.h"
#include "config.h"
#include "address.h"
//#include "utility/expected.h"
#include <memory>
#include <functional>
#include <unordered_map>

namespace beam { namespace io {

class TcpStream;

class Reactor : public std::enable_shared_from_this<Reactor> {
public:
    Reactor(const Reactor&) = delete;
    Reactor& operator=(const Reactor&) = delete;

    using Ptr = std::shared_ptr<Reactor>;
    
    /// Creates a new reactor.
    /// NOTE: throws on errors
    static Ptr create(const Config& config);

    //TODO static expected<Ptr, int> create(const Config& config);

    /// Performs shutdown and cleanup.
    ~Reactor();

    /// Runs the reactor. This function blocks.
    void run();

    /// Stops the running reactor.
    /// NOTE: Called from another thread.
    void stop();

    /// Used to avoid throwing in many situations (from callbacks etc)
    int get_last_error() const { return _lastError; }

    const Config& config() const { return _config; }

    using ConnectCallback = std::function<void(uint64_t tag, std::unique_ptr<TcpStream>&& newStream, int status)>;

    // TODO expected
    bool tcp_connect(Address address, uint64_t tag, const ConnectCallback& callback);

    void cancel_tcp_connect(uint64_t tag);

private:
    Reactor(const Config& config);

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
            if (_reactor && _handle) _reactor->async_close(_handle);
        }

        Reactor::Ptr _reactor;
        uv_handle_t* _handle=0;
    };

    struct ConnectContext {
        uint64_t tag;
        ConnectCallback callback;
        uv_connect_t request;
    };

    void connect_callback(ConnectContext* ctx, int status);

    bool init_asyncevent(Object* o, uv_async_cb cb);

    bool init_timer(Object* o);
    bool start_timer(Object* o, unsigned intervalMsec, bool isPeriodic, uv_timer_cb cb);
    void cancel_timer(Object* o);

    bool init_tcpserver(Object* o, Address bindAddress, uv_connection_cb cb);
    bool init_tcpstream(Object* o);
    int accept_tcpstream(Object* acceptor, Object* newConnection);

    bool init_object(int status, Object* o, uv_handle_t* h);
    void async_close(uv_handle_t*& handle);

    union Handles {
        uv_timer_t timer;
        uv_async_t async;
        uv_tcp_t tcp;
    };

    Config _config;
    uv_loop_t _loop;
    uv_async_t _stopEvent;
    MemPool<uv_handle_t, sizeof(Handles)> _handlePool;
    std::unordered_map<uint64_t, ConnectContext> _connectRequests;
    int _lastError=0;

    friend class AsyncEvent;
    friend class Timer;
    friend class TcpServer;
    friend class TcpStream;
};

}} //namespaces
