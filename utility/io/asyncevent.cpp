#include "asyncevent.h"
#include "exception.h"
#include <assert.h>

namespace beam { namespace io {

AsyncEvent::Ptr AsyncEvent::create(const Reactor::Ptr& reactor, AsyncEvent::Callback&& callback) {
    assert(reactor);
    assert(callback);
    
    if (!reactor || !callback)
        IO_EXCEPTION(EINVAL);
    
    Ptr event(new AsyncEvent(std::move(callback)));
    int errorCode = reactor->init_asyncevent(
        event.get(),
        [](uv_async_t* handle) {
            assert(handle);
            AsyncEvent* ae = reinterpret_cast<AsyncEvent*>(handle->data);
            if (ae) ae->_callback();
        }
    );
    if (errorCode) IO_EXCEPTION(errorCode);
    return event;
}

AsyncEvent::AsyncEvent(AsyncEvent::Callback&& callback) :
    _callback(std::move(callback)) //, _valid(true)
{}

AsyncEvent::~AsyncEvent() {
    // before async_close
    //_valid = false;
}

expected<void, int> AsyncEvent::trigger() {
    int errorCode = uv_async_send((uv_async_t*)_handle);
    if (errorCode) {
        return make_unexpected(errorCode);
        // TODO log on errors
    }
    return ok();
}

}} //namespaces

