#pragma once
#include "reactor.h"
#include <functional>

namespace io {

class AsyncEvent : protected Reactor::Object {
public:
    using Callback = std::function<void()>;

    // Must be created in reactor thread only
    AsyncEvent(Reactor::Ptr reactor, Callback&& callback);

    ~AsyncEvent();

    /// Posts the event. Can be triggered from any thread
    void trigger();

private:
    Callback _callback;
};

} //namespace

