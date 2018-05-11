#include "timer.h"
#include <assert.h>

#define LOG_VERBOSE_ENABLED 1
#include "utility/logger.h"

namespace beam { namespace io {

Timer::Ptr Timer::create(const Reactor::Ptr& reactor) {
    assert(reactor);
    Ptr timer(new Timer());
    ErrorCode errorCode = reactor->init_timer(timer.get());
    IO_EXCEPTION_IF(errorCode);
    return timer;
}

Timer::~Timer() {
    LOG_VERBOSE() << ".";
}

Result Timer::start(unsigned intervalMsec, bool isPeriodic, Callback&& callback) {
    assert(callback);
    _callback = std::move(callback);
    if (intervalMsec == unsigned(-1)) {
        // just set callback
        return Ok();
    }
    return restart(intervalMsec, isPeriodic);
}

Result Timer::restart(unsigned intervalMsec, bool isPeriodic) {
    assert(_callback);

    return make_result(
        _reactor->start_timer(
            this,
            intervalMsec,
            isPeriodic,
            [](uv_timer_t* handle) {
                assert(handle);
                Timer* t = reinterpret_cast<Timer*>(handle->data);
                if (t) t->_callback();
            }
        )
    );
}

void Timer::cancel() {
    _reactor->cancel_timer(this);
    _callback = []{};
}

}} //namespaces

