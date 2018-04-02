#pragma once
#include "reactor.h"

namespace io {

class Timer : protected Reactor::Object {
public:
    using Ptr = std::shared_ptr<Timer>;
    using Callback = std::function<void()>;
    
    // Doesn't throw, returns empty ptr on error, see Reactor::get_last_error()
    static Ptr create(const Reactor::Ptr& reactor);
    
    // On errors, see Reactor::get_last_error()
    bool start(unsigned intervalMsec, bool isPeriodic, Callback&& callback);

    // does nothing if inactive
    void cancel();

private:
    Timer() = default;
    
    Callback _callback;
};

} //namespace

