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
#include "utility/message_queue.h"
#include "utility/helpers.h"

namespace beam {

/// Inter-thread bridge template
template <typename Interface> class Bridge : public Interface {
public:
    /// Functions with captured args go into the queue
    using BridgeMessage = std::function<void(Interface& receiver)>;

    /// Macros helper
    using BridgeInterface = Interface;

    /// Sets up the channel
    Bridge(Interface& _forwardTo, io::Reactor& _reactor) :
        receiver(_forwardTo),
        rx(_reactor, BIND_THIS_MEMFN(on_rx)),
        tx(rx.get_tx())
    {}

    /// Shutdowns receiver side of channel
    void stop_rx() {
        rx.close();
    }

// Default initialization for derived classes
#define BRIDGE_INIT(DerivedClassName) \
    DerivedClassName(BridgeInterface& _forwardTo, io::Reactor& _reactor) : \
        Bridge<BridgeInterface>(_forwardTo, _reactor) \
    {}

// Use in derived classes to implement Interface's functions like this:
// E.g. to implement
// struct ISomeInterface { ...
//      virtual void send_req_1(uint64_t to, Req1&& r)=0;
//      ... };
// do this in proxy class derived from Bridge<IsomeInterface>:
// struct SomeInterfaceBridge { ...
//      BRIDGE_FORWARD_IMPL(send_req_1, Req1); ... }
#define BRIDGE_FORWARD_IMPL(Func, MessageObject) \
    void Func(uint64_t to, MessageObject&& r) { \
        tx.send( \
            [to, r{std::move(r)} ](BridgeInterface& receiver_) mutable { \
                receiver_.Func(to, std::move(r)); \
            } \
        ); \
    }

    template <typename F, typename ...Args>
    void call_async(F&& func,  Args... args)
    {
        tx.send(
            [func, args...](BridgeInterface& receiver_) mutable
            {
                (receiver_.*func)(std::forward<Args>(args)...);
            }
        );
    }

protected:
    BridgeInterface& receiver;
    RX<BridgeMessage> rx;
    TX<BridgeMessage> tx;

private:
    void on_rx(BridgeMessage&& msg) {
        msg(receiver);
    }
};

} //namespace
