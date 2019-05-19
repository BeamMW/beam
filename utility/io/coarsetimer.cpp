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

#include "coarsetimer.h"
#include "utility/helpers.h"
#include <assert.h>

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif
#include "utility/logger.h"

namespace beam { namespace io {

CoarseTimer::Ptr CoarseTimer::create(Reactor& reactor, unsigned resolutionMsec, const Callback& cb) {
    assert(cb);
    assert(resolutionMsec > 0);

    if (!cb || !resolutionMsec) IO_EXCEPTION(EC_EINVAL);

    return CoarseTimer::Ptr(new CoarseTimer(resolutionMsec, cb, Timer::create(reactor)));
}

CoarseTimer::CoarseTimer(unsigned resolutionMsec, const Callback& cb, Timer::Ptr&& timer) :
    _resolution(resolutionMsec),
    _callback(cb),
    _timer(std::move(timer))
{
    auto result = _timer->start(unsigned(-1), false, BIND_THIS_MEMFN(on_timer));
    if (!result) IO_EXCEPTION(result.error());
}

CoarseTimer::~CoarseTimer() {
    assert(!_insideCallback && "attempt to delete coarse timer from inside its callback, unsupported feature");
}

static inline uint64_t mono_clock() {
    return uv_hrtime() / 1000000; //nsec->msec, monotonic clock
}

Result CoarseTimer::set_timer(unsigned intervalMsec, ID id) {
    if (_validIds.count(id)) {
        LOG_DEBUG() << "coarse timer: existing id " << std::hex << id << std::dec;
        return make_unexpected(EC_EINVAL);
    }
    if (intervalMsec > 0 && intervalMsec < unsigned(-1) - _resolution) {
        // if 0 then callback will fire on next event loop cycle, otherwise adjust to coarse resolution
        intervalMsec -= ((intervalMsec + _resolution) % _resolution);
    }
    Clock now = mono_clock();
    Clock clock = now + intervalMsec;
    _queue.insert({ clock, id });
    _validIds.insert({ id, clock });
    if (!_insideCallback && _timerSetTo > clock) {
        LOG_VERBOSE() << TRACE(intervalMsec);
        _timerSetTo = clock;
        return _timer->restart(intervalMsec, false);
    }
    return Ok();
}

void CoarseTimer::cancel(ID id) {
    _validIds.erase(id);
    if (_validIds.empty()) cancel_all();
}

void CoarseTimer::cancel_all() {
    _validIds.clear();
    _queue.clear();
    if (_timerSetTo != NEVER) {
        _timer->cancel();
        _timerSetTo = NEVER;
    }
}

// uv timers inaccurate intervals
static constexpr unsigned TIMER_ACCURACY = 10;

void CoarseTimer::on_timer() {
    LOG_VERBOSE() << TRACE(_queue.size());

    if (_queue.empty()) return;
    Clock now = mono_clock();

    _insideCallback = true;

    Clock clock = 0;

    while (!_queue.empty()) {
        auto it = _queue.begin();

        clock = it->first;

        LOG_VERBOSE() << TRACE(now) << TRACE(clock) << TRACE(_timerSetTo);

        if (clock > now + TIMER_ACCURACY) break;
        ID id = it->second;

        LOG_VERBOSE() << TRACE(id);

        // this helps calling set_timer(), cancel(), cancel_all() from inside callbacks
        _queue.erase(it);

        auto v = _validIds.find(id);
        if (v != _validIds.end()) {
            if (v->second == clock) {
                _validIds.erase(v);
                _callback(id);
            }
        }
    }

    _insideCallback = false;

    if (_queue.empty()) {
        cancel_all();
    } else {
        now = mono_clock();
        unsigned intervalMsec = 0;
        if (clock > now) intervalMsec = unsigned(clock - now);
        LOG_VERBOSE() << TRACE(intervalMsec);
        Result res =_timer->restart(intervalMsec, false);
        if (!res) {
            LOG_ERROR() << "cannot restart timer, code=" << res.error();
        } else {
            _timerSetTo = now + intervalMsec;
        }
    }
}

MultipleTimers::MultipleTimers(Reactor& reactor, unsigned resolutionMsec) :
    _timer(CoarseTimer::create(reactor, resolutionMsec, BIND_THIS_MEMFN(on_timer)))
{}

io::Result MultipleTimers::set_timer(CoarseTimer::ID id, unsigned intervalMsec, Timer::Callback&& callback) {
    if (!callback) return make_unexpected(io::EC_EINVAL);

    auto it = _timerCallbacks.find(id);
    if (it != _timerCallbacks.end()) {
        _timer->cancel(id);
        it->second = std::move(callback);
    } else {
        _timerCallbacks.insert( { id, std::move(callback) } );
    }

    io::Result res = _timer->set_timer(intervalMsec, id);
    if (!res) {
        _timerCallbacks.erase(id);
    }

    return res;
}

void MultipleTimers::cancel(CoarseTimer::ID id) {
    if (_timerCallbacks.erase(id) != 0) {
        _timer->cancel(id);
    }
}

void MultipleTimers::cancel_all() {
    _timer->cancel_all();
    _timerCallbacks.clear();
}

void MultipleTimers::on_timer(CoarseTimer::ID id) {
    auto it = _timerCallbacks.find(id);
    if (it != _timerCallbacks.end()) {
        Timer::Callback cb = std::move(it->second);
        _timerCallbacks.erase(it);
        cb();
    }
}

}} //namespaces
