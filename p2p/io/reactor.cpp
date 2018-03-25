#include "reactor.h"
#include "exception.h"

namespace io {

Reactor::Ptr Reactor::create() {
    return Reactor::Ptr(new Reactor());
}

Reactor::Reactor() {
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


} //namespace
