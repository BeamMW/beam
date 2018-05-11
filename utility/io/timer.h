#pragma once
#include "reactor.h"

namespace beam { namespace io {

class Timer : protected Reactor::Object {
public:
    using Ptr = std::unique_ptr<Timer>;
    using Callback = std::function<void()>;

    /// Creates a new timer object, throws on errors
    static Ptr create(const Reactor::Ptr& reactor);

    /// Starts the timer
    Result start(unsigned intervalMsec, bool isPeriodic, Callback&& callback);
    
    /// Restarts the timer if callbackis already set
    Result restart(unsigned intervalMsec, bool isPeriodic);

    /// Cancels the timer. May be called from anywhere in timer's thread
    void cancel();
    
    ~Timer();

private:
    Timer() = default;

    Callback _callback;
};

}} //namespaces

