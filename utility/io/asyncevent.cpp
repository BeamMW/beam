#include "asyncevent.h"
#include <assert.h>

namespace io {

AsyncEvent::Ptr AsyncEvent::create(const Reactor::Ptr& reactor, AsyncEvent::Callback&& callback) {
    assert(reactor);
    Ptr event(new AsyncEvent(std::move(callback)));  
    if (reactor->init_asyncevent(
        event.get(),
        [](uv_async_t* handle) {
            assert(handle);
            assert(handle->data);
            reinterpret_cast<AsyncEvent*>(handle->data)->_callback();
        }
        
    )) {
        return event;
    }
    return Ptr();    
}
    
AsyncEvent::AsyncEvent(AsyncEvent::Callback&& callback) :
    _callback(std::move(callback))
{}

bool AsyncEvent::trigger() {
    // TODO atomics on handle!
    
    return (_handle && uv_async_send((uv_async_t*)_handle) == 0);

    // TODO log on errors
}

} //namespace

