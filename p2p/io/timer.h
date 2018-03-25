#pragma once
#include "reactor.h"
#include <functional>

namespace io {

class Timer : protected Reactor::Object {
public:
    using Callback = std::function<void()>;

    explicit Timer(Reactor::Ptr reactor);

    ~Timer();

    void start(unsigned intervalMsec, bool isPeriodic, Callback&& callback);

    // does nothing if inactive
    void cancel();

private:
    Callback _callback;
};

} //namespace

