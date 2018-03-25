#pragma once
#include "io/libuv/include/uv.h"
#include <memory>

namespace io {

class Reactor : public std::enable_shared_from_this<Reactor> {
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

    uv_loop_t  _loop;
    uv_async_t _stopEvent;

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

        explicit Object(Reactor::Ptr reactor, bool emptyObject=false) :
            _reactor(reactor),
            _handle(emptyObject ? 0 : _reactor->init_object(this))
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

    uv_handle_t* init_object(Object* o);
    void async_close(uv_handle_t*& handle);
    void release(uv_handle_t* handle);

    friend class AsyncEvent;
    friend class TcpServer;
    friend class TcpStream;
};

}
