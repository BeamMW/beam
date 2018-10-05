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

#include "asynccontext.h"
#include "logger.h"
#include <assert.h>

namespace beam {

static thread_local AsyncContext* tls_ctx = 0;

namespace async {

AsyncContext* ctx() { return tls_ctx; }

} //namespace

AsyncContext::AsyncContext(unsigned coarseTimerResolutionMsec) :
    _reactor(io::Reactor::create()),
    _timers(*_reactor, coarseTimerResolutionMsec),
    _started(false)
{
    attach_to_thread();
}

AsyncContext::~AsyncContext() {
    if (_thread) _thread.join();
    else detach_from_thread();
}

void AsyncContext::attach_to_thread() {
    if (tls_ctx != this) {
        _prevInThread = tls_ctx;
        tls_ctx = this;
    }
}

void AsyncContext::detach_from_thread() {
    if (tls_ctx == this) {
        tls_ctx = _prevInThread;
        _prevInThread = 0;
    }
}

void AsyncContext::run_in_this_thread() {
    _reactor->run();
}

void AsyncContext::run_async(AsyncContext::RunCallback&& beforeRun, AsyncContext::RunCallback&& afterRun) {
    if (_started) {
        throw std::runtime_error("async context: already started");
    }
    detach_from_thread();
    _thread.start(BIND_THIS_MEMFN(thread_func), std::move(beforeRun), std::move(afterRun));
    while (!_started) std::this_thread::yield();
}

void AsyncContext::thread_func(AsyncContext::RunCallback&& beforeRun, AsyncContext::RunCallback&& afterRun) {
    block_signals_in_this_thread();
    attach_to_thread();
    _started = true;
    LOG_DEBUG() << "starting, thread=" << get_thread_id();
    try {
        if (beforeRun) beforeRun();
        _reactor->run();
        if (afterRun) afterRun();
    } catch (const std::exception& e) {
        LOG_CRITICAL() << "Unhandled exception in child thread, what=" << e.what();
        assert(false && "exception in child thread");
    } catch (...) {
        LOG_CRITICAL() << "Unhandled non-std exception in child thread";
        assert(false && "exception in child thread");
    }
    LOG_DEBUG() << "exiting, thread=" << get_thread_id();
    detach_from_thread();
}

void AsyncContext::stop() {
    _reactor->stop();
}

void AsyncContext::wait() {
    _thread.join();
    attach_to_thread();
    _started = false;
}

io::Result AsyncContext::set_coarse_timer(AsyncContext::TimerID id, unsigned intervalMsec, AsyncContext::TimerCallback&& callback) {
    return _timers.set_timer(id, intervalMsec, std::move(callback));
}

void AsyncContext::cancel_coarse_timer(AsyncContext::TimerID id) {
    _timers.cancel(id);
}

} //namespace
