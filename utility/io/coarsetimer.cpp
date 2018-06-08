#include "coarsetimer.h"
#include "utility/helpers.h"
#include <assert.h>

#define LOG_VERBOSE_ENABLED 1
#include "utility/logger.h"

namespace beam { namespace io {

CoarseTimer::Ptr CoarseTimer::create(const Reactor::Ptr& reactor, unsigned resolutionMsec, const Callback& cb) {
    assert(reactor);
    assert(cb);
    assert(resolutionMsec > 0);

    if (!reactor || !cb || !resolutionMsec) IO_EXCEPTION(EC_EINVAL);

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
    if (_validIds.count(id)) return make_unexpected(EC_EINVAL);
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

}} //namespaces
