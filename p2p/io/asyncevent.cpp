#include "asyncevent.h"
#include "exception.h"

namespace io {

AsyncEvent::AsyncEvent(Reactor::Ptr _reactor, AsyncEvent::Callback&& callback) :
    Reactor::Object(_reactor),
    _callback(std::move(callback))
{
    int r = uv_async_init(
        &(_reactor->_loop),
        (uv_async_t*)_handle,
        [](uv_async_t* handle) {
            reinterpret_cast<AsyncEvent*>(handle->data)->_callback();
        }
    );

    if (r != 0) IO_EXCEPTION(r, "cannot initialize AsyncEvent");
}

AsyncEvent::~AsyncEvent()
{}

void AsyncEvent::trigger() {
    uv_async_send((uv_async_t*)_handle);

    // TODO log on errors
}

} //namespace

