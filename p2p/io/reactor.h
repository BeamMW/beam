#pragma once
#include "io/libuv/include/uv.h"
#include <memory>
#include <vector>
#include <string.h>

namespace io {

class Reactor /*: public std::enable_shared_from_this<Reactor>*/ {
public:
    Reactor(const Reactor&) = delete;
    Reactor& operator=(const Reactor&) = delete;

    using Ptr = std::shared_ptr<Reactor>;

    /// Creates a new reactor.
    /// NOTE: throws on errors
    static Ptr create();

    /// Performs shutdown and cleanup.
    ~Reactor();

    /// Runs the reactor. This function blocks.
    void run();

    /// Stops the running reactor.
    /// NOTE: Called from another thread.
    void stop();

private:
    Reactor();

    class HandlePool {
        static const size_t DATA_SIZE = sizeof(uv_any_handle);
    public:
        explicit HandlePool(unsigned maxSize) :
            _maxSize(maxSize)
        {}

        ~HandlePool() {
            for (uv_handle_t* h: _pool) {
                free(h);
            }
        }

        uv_handle_t* alloc() {
            uv_handle_t* r = 0;
            if (!_pool.empty()) {
                r = _pool.back();
                _pool.pop_back();
            } else {
                r = (uv_handle_t*)calloc(1, DATA_SIZE);
            }
            return r;
        }

        void release(uv_handle_t* h) {
            if (_pool.size() > _maxSize) {
                free(h);
            } else {
                memset(h, 0, DATA_SIZE);
                _pool.push_back(h);
            }
        }

    private:
        using Pool = std::vector<uv_handle_t*>;

        Pool _pool;
        unsigned _maxSize;
    };
    
    class Object {
    protected:
        Object() = delete;
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

        explicit Object(Reactor::Ptr reactor) :
            _reactor(reactor),
            _handle(_reactor->init_object(this))
        {}

        Object(Reactor::Ptr reactor, uv_handle_t* handle) :
            _reactor(reactor),
            _handle(handle)
        {}

        ~Object() {
            async_close();
        }

        void async_close() {
            _reactor->async_close(_handle);
        }

        Reactor::Ptr _reactor;
        uv_handle_t* _handle;
    };

    uv_handle_t* new_handle();
    uv_handle_t* init_object(Object* o);
    void async_close(uv_handle_t*& handle);
    void release(uv_handle_t* handle);

    friend class AsyncEvent;
    friend class Timer;
    friend class TcpServer;
    friend class TcpStream;
    friend class TcpConnector;
    
    uv_loop_t  _loop;
    uv_async_t _stopEvent;
    HandlePool _handlePool;
};

}
