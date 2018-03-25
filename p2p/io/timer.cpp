#include "timer.h"
#include "exception.h"
#include <assert.h>

namespace io {

Timer::Timer(Reactor::Ptr reactor) :
    Reactor::Object(reactor)
{
    int r = uv_timer_init(
        &(_reactor->_loop),
        (uv_timer_t*)_handle
    );
    if (r != 0) IO_EXCEPTION(r, "cannot initialize timer");
}

Timer::~Timer()
{}

void Timer::start(unsigned intervalMsec, bool isPeriodic, Callback&& callback) {
    assert(callback);
    cancel();

    _callback = std::move(callback);
    int r = uv_timer_start(
        (uv_timer_t*)_handle,
        [](uv_timer_t* handle) {
            reinterpret_cast<Timer*>(handle->data)->_callback();
        },
        intervalMsec,
        isPeriodic ? intervalMsec : 0
    );
    if (r != 0) IO_EXCEPTION(r, "cannot start timer");
}

void Timer::cancel() {
    if (uv_is_active(_handle)) {
        int r = uv_timer_stop((uv_timer_t*)_handle);
        if (r != 0) {
            //TODO log
            return;
        }
        _callback = Callback();
    }
}

} //namespace

