// Copyright 2018-2020 The Beam Team
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

#include <memory>
#include <queue>
#include <mutex>
#include <thread>
#include "utility/io/reactor.h"
#include "utility/io/asyncevent.h"

class SafeReactor: std::enable_shared_from_this<SafeReactor>
{
public:
    SafeReactor(const SafeReactor&) = delete;
    SafeReactor& operator=(const SafeReactor&) = delete;
    SafeReactor() = default;

    using Ptr = std::shared_ptr<SafeReactor>;
    using Callback = beam::io::AsyncEvent::Callback;
    static Ptr create();

    void callAsync(Callback cback);
    void assert_thread();
    beam::io::Reactor& ref ();
    beam::io::Reactor::Ptr ptr();

private:
    beam::io::Reactor::Ptr    _reactor;
    beam::io::AsyncEvent::Ptr _event;
    std::thread::id           _reactorThread;

    std::mutex _queueMutex;
    std::queue<Callback> _queue;
};
