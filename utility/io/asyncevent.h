#pragma once
#include "reactor.h"

namespace beam { namespace io {

/// Async event that can be triggered from any thread
class AsyncEvent : protected Reactor::Object, public std::enable_shared_from_this<AsyncEvent> {
public:
    using Ptr = std::shared_ptr<AsyncEvent>;
    using Callback = std::function<void()>;

    /// Creates async event object, throws on errors
    static Ptr create(const Reactor::Ptr& reactor, Callback&& callback);

    /// Posts the event. Can be triggered from any thread
    Result post();

    struct Trigger {
        Trigger(const AsyncEvent::Ptr& ae) : _event(ae) {}

        Result operator()() {
            auto e = _event.lock();
            if (e) return e->post();
            return make_unexpected(EC_EINVAL);
        }

    private:
        std::weak_ptr<AsyncEvent> _event;
    };

    Trigger get_trigger() { return Trigger(shared_from_this()); }

    ~AsyncEvent();

private:
    explicit AsyncEvent(Callback&& callback);

    Callback _callback;
};

}} //namespaces

