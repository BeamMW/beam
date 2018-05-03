#include "timer.h"
#include "exception.h"
#include <assert.h>

namespace beam { namespace io {

Timer::Ptr Timer::create(const Reactor::Ptr& reactor) {
    assert(reactor);
    Ptr timer(new Timer());
    int errorCode = reactor->init_timer(timer.get());
    IO_EXCEPTION_IF(errorCode);
    return timer;
}

expected<void,int> Timer::start(unsigned intervalMsec, bool isPeriodic, Callback&& callback) {
    assert(callback);
    _callback = std::move(callback);
    return restart(intervalMsec, isPeriodic);
}

expected<void,int> Timer::restart(unsigned intervalMsec, bool isPeriodic) {
    assert(_callback);

    int errorCode = _reactor->start_timer(
        this,
        intervalMsec,
        isPeriodic,
        [](uv_timer_t* handle) {
            assert(handle);
            Timer* t = reinterpret_cast<Timer*>(handle->data);
            if (t) t->_callback();
        }
    );

    if (errorCode != 0) return make_unexpected(errorCode);
           
    return ok();
}

void Timer::cancel() {
    _reactor->cancel_timer(this);
    _callback = []{};
}

}} //namespaces

