#pragma once
#include "io/libuv/include/uv.h"
#include <memory>

namespace io {

class Reactor : public std::enable_shared_from_this<Reactor> {
public:
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

    struct Object {
        Reactor::Ptr reactor;
        void* handle;
    };
private:
    Reactor();

    uv_loop_t  _loop;
    uv_async_t _stopEvent;
};

}
