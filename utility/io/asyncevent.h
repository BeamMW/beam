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
#include "reactor.h"

namespace beam { namespace io {

/// Async event that can be triggered from any thread
class AsyncEvent : protected Reactor::Object, public std::enable_shared_from_this<AsyncEvent> {
public:
    using Ptr = std::shared_ptr<AsyncEvent>;
    using Callback = std::function<void()>;

    /// Creates async event object, throws on errors
    static Ptr create(Reactor& reactor, Callback&& callback);

    /// Posts the event. Can be triggered from any thread
    Result post();

    struct Trigger {
        Trigger() = default;

        Trigger(const AsyncEvent::Ptr& ae) : _event(ae) {}

        Trigger& operator=(const AsyncEvent::Ptr& ae) {
            _event = ae;
            return *this;
        }

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

