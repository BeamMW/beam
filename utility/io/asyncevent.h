#pragma once
#include "reactor.h"

namespace beam { namespace io {

class AsyncEvent : protected Reactor::Object {
public:
    using Ptr = std::shared_ptr<AsyncEvent>;
    using Callback = std::function<void()>;

    static Ptr create(const Reactor::Ptr& reactor, Callback&& callback);

    /// Posts the event. Can be triggered from any thread
    /// On errors, see Reactor::get_last_error()
    bool trigger();

private:
    explicit AsyncEvent(Callback&& callback);

    Callback _callback;
};

}} //namespaces

