#include "coarsetimer.h"
#include "utility/helpers.h"
#include <assert.h>

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

static inline uint64_t mono_clock() {
    return uv_hrtime() / 1000000; //nsec->msec, monotonic clock
}

expected<void, ErrorCode> CoarseTimer::set_timer(unsigned intervalMsec, ID id) {
    if (_validIds.count(id)) return make_unexpected(EC_EINVAL);
    if (intervalMsec > 0 && intervalMsec < unsigned(-1) - _resolution) {
        // if 0 then callback will fire on next event loop cycle, otherwise adjust to coarse resolution
        intervalMsec -= ((intervalMsec + _resolution) % _resolution);
    }
    Clock now = mono_clock();
    Clock clock = now + intervalMsec;
    _queue.insert({ clock, id });
    _validIds.insert({ id, clock });
    update_timer(now);
    return ok();
}

void CoarseTimer::cancel(ID id) {
    _validIds.erase(id);
    if (_validIds.empty()) cancel_all();
}

void CoarseTimer::cancel_all() {
    _validIds.clear();
    _queue.clear();
    if (_nextTime > 0) {
        _timer->cancel();
        _nextTime = 0;
    }
}

void CoarseTimer::update_timer(CoarseTimer::Clock now) {
    assert (!_queue.empty());
    Clock next = _queue.begin()->first;
    if (_nextTime != next) {
        _nextTime = (now < next) ? next : now;
        _timer->restart(unsigned(next-now), false);
    }
}

void CoarseTimer::on_timer() {
    if (_queue.empty()) return;
    Clock now = mono_clock();
    auto it = _queue.begin();
    for (auto end = _queue.end(); it != end; ++it) {
        Clock clock = it->first;
        if (clock > now) break;
        ID id = it->second;
        auto v = _validIds.find(id);
        if (v != _validIds.end()) {
            if (v->second == clock) {
                _validIds.erase(v);
                _callback(id);
            }
        }
    }
    _queue.erase(_queue.begin(), it);
    if (_queue.empty()) {
        cancel_all();
    } else {
        update_timer(now);
    }
}
    
}} //namespaces

