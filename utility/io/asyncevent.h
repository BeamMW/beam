#pragma once
#include "reactor.h"
//#include <atomic>

namespace beam { namespace io {

/// Async event that can be triggered from any thread
class AsyncEvent : protected Reactor::Object {
public:
    using Ptr = std::shared_ptr<AsyncEvent>;
    using Callback = std::function<void()>;

    /// Creates async event object, throws on errors
    static Ptr create(const Reactor::Ptr& reactor, Callback&& callback);

    /// Posts the event. Can be triggered from any thread
    expected<void,int> trigger();

    ~AsyncEvent();
    
private:
    explicit AsyncEvent(Callback&& callback);

    Callback _callback;
    //std::atomic<bool> _valid;
};

}} //namespaces

