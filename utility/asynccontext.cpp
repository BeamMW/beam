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
    _timer(io::CoarseTimer::create(_reactor, coarseTimerResolutionMsec, BIND_THIS_MEMFN(on_coarse_timer)))
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
    detach_from_thread();
    _thread.start(BIND_THIS_MEMFN(thread_func), std::move(beforeRun), std::move(afterRun));
}
    
void AsyncContext::thread_func(AsyncContext::RunCallback&& beforeRun, AsyncContext::RunCallback&& afterRun) {
    attach_to_thread();
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
}

io::Result AsyncContext::set_coarse_timer(AsyncContext::ID id, unsigned intervalMsec, AsyncContext::TimerCallback&& callback) {
    if (_timerCallbacks.count(id) || !callback) return make_unexpected(io::EC_EINVAL);
    io::Result res = _timer->set_timer(intervalMsec, id);
    if (res) {
        _timerCallbacks.insert( { id, std::move(callback) } );
    }
    return res;
}

void AsyncContext::on_coarse_timer(AsyncContext::ID id) {
    auto it = _timerCallbacks.find(id);
    if (it != _timerCallbacks.end()) {
        TimerCallback cb = it->second;
        _timerCallbacks.erase(it);
        cb(id);
    }
}
    
void AsyncContext::cancel_coarse_timer(AsyncContext::ID id) {
    if (_timerCallbacks.erase(id) != 0) {
        _timer->cancel(id);
    }
}
    
} //namespace
