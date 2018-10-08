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
#include "helpers.h"
#include "io/coarsetimer.h"
#include <unordered_map>
#include <atomic>

namespace beam {

class AsyncContext;

// Current AsyncContext API
namespace async {

/// Returns current context if any
AsyncContext* ctx();

// TODO other stuff

} //namespace

/// Wrapper for asynchrony
class AsyncContext {
public:
    // TODO: unify IDs
    using TimerID = io::CoarseTimer::ID;
    using TimerCallback = io::Timer::Callback;

    explicit AsyncContext(unsigned coarseTimerResolutionMsec=100);

    /// Dtor, allows for derived classes if needed
    virtual ~AsyncContext();

    /// Runs event loop in this thread, blocks
    void run_in_this_thread();

    using RunCallback = std::function<void()>;

    /// Spawns the dedicated thread and runs there
    void run_async(RunCallback&& beforeRun=RunCallback(), RunCallback&& afterRun=RunCallback());

    /// Stops the event loop, can be called from any thread
    void stop();

    /// Waits for thread, if any is running
    void wait();

    /// Sets one-shot coarse timer
    io::Result set_coarse_timer(TimerID id, unsigned intervalMsec, TimerCallback&& callback);

    /// Cancels one-shot timer
    void cancel_coarse_timer(TimerID id);

    /// periodic timer
    io::Timer::Ptr set_timer(unsigned periodMsec, io::Timer::Callback&& onTimer) {
        io::Timer::Ptr timer(io::Timer::create(*_reactor));
        timer->start(periodMsec, true, std::move(onTimer));
        return timer;
    }

private:
    /// Thread function
    void thread_func(RunCallback&& beforeRun, RunCallback&& afterRun);

    /// Internal timer callback
    void on_coarse_timer(TimerID id);


    void attach_to_thread();
    void detach_from_thread();

protected:
    io::Reactor::Ptr _reactor;

private:
    io::MultipleTimers _timers;
    Thread _thread;
    AsyncContext* _prevInThread=0;
    std::atomic<bool> _started;
};

} //namespace
