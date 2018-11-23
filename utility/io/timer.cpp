// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "timer.h"
#include <assert.h>

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif
#include "utility/logger.h"

namespace beam { namespace io {

Timer::Ptr Timer::create(Reactor& reactor) {
    Ptr timer(new Timer());
    ErrorCode errorCode = reactor.init_timer(timer.get());
    IO_EXCEPTION_IF(errorCode);
    return timer;
}

Result Timer::start(unsigned intervalMsec, bool isPeriodic, Callback callback) {
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

