#include "timer.h"
#include <assert.h>

namespace io {

Timer::Ptr Timer::create(const Reactor::Ptr& reactor) {
    assert(reactor);
    Ptr timer(new Timer());  
    if (reactor->init_timer(timer.get())) {
        return timer;
    }
    return Ptr();
} 

bool Timer::start(unsigned intervalMsec, bool isPeriodic, Callback&& callback) {
    assert(callback);
    cancel();

    bool ok = _reactor->start_timer(
        this,
        intervalMsec,
        isPeriodic,
        [](uv_timer_t* handle) {
            assert(handle);
            assert(handle->data);
            reinterpret_cast<Timer*>(handle->data)->_callback();
        }
    );
    
    if (ok) {
        _callback = std::move(callback);
    }
    
    return ok;
}

void Timer::cancel() {
    _reactor->cancel_timer(this);
    _callback = Callback();
}

} //namespace

