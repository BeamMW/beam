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

#pragma once
#include "timer.h"
#include <map>
#include <vector>
#include <limits>

namespace beam { namespace io {

/// Coarse timer helper, for connect/reconnect timers
class CoarseTimer {
public:
    using ID = uint64_t;
    using Callback = std::function<void(ID)>;
    using Ptr = std::unique_ptr<CoarseTimer>;

    /// Creates coarse timer, throws on errors
    static Ptr create(Reactor& reactor, unsigned resolutionMsec, const Callback& cb);

    /// Sets up timer callback for id, EC_EINVAL if id is already there or on timer setup failure
    Result set_timer(unsigned intervalMsec, ID id);

    /// Cancels callback for id
    void cancel(ID id);

    /// Cancels all callbacks
    void cancel_all();

    ~CoarseTimer();

private:
    CoarseTimer(unsigned resolutionMsec, const Callback& cb, Timer::Ptr&& timer);

    /// Internal callback
    void on_timer();

    /// abs. time
    using Clock = uint64_t;
    static constexpr Clock NEVER = std::numeric_limits<Clock>::max();

    /// Flag that prevents from updating timer too often
    bool _insideCallback=false;

    /// Coarse msec resolution
    const unsigned _resolution;

    /// External callback
    Callback _callback;

    /// Timers queue
    std::multimap<Clock, ID> _queue;

    /// Valid Ids
    std::map<ID, Clock> _validIds;

    /// Next time to wake
    Clock _timerSetTo=NEVER;

    /// Timer object
    Timer::Ptr _timer;
};

/// Multiple timers across one coarse timer
class MultipleTimers {
public:
    MultipleTimers(Reactor& reactor, unsigned resolutionMsec);

    /// Sets one-shot coarse timer
    io::Result set_timer(CoarseTimer::ID id, unsigned intervalMsec, Timer::Callback&& callback);

    /// Cancels one-shot timer
    void cancel(CoarseTimer::ID id);

    /// Cancels all timers
    void cancel_all();

private:
    void on_timer(CoarseTimer::ID id);

    std::map<CoarseTimer::ID, Timer::Callback> _timerCallbacks;
    io::CoarseTimer::Ptr _timer;
};

}} //namespaces
