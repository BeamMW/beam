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
#include "reactor.h"
#include <assert.h>

namespace beam
{
    SafeReactor::Ptr SafeReactor::create()
    {
        auto reactor = std::make_shared<SafeReactor>();

        reactor->_reactorThread = std::this_thread::get_id();
        reactor->_reactor = beam::io::Reactor::create();
        reactor->_event = beam::io::AsyncEvent::create(*reactor->_reactor, [pr = reactor.get()]{
            // We need to process all the pending events in the queue
            // since libuv may combine multiple post() calls into one
            while (true)
            {
                beam::io::AsyncEvent::Callback cback;
                {
                    std::unique_lock lock(pr->_queueMutex);
                    if (pr->_queue.empty()) {
                        return;
                    }

                    cback = std::move(pr->_queue.front());
                    pr->_queue.pop();
                }
                cback();
            }
            });

        return reactor;
    }

    io::Reactor& SafeReactor::ref()
    {
        assert_thread();
        return *_reactor;
    }

    io::Reactor::Ptr SafeReactor::ptr()
    {
        assert_thread();
        return _reactor;
    }

    void SafeReactor::assert_thread()
    {
        if (_reactorThread != std::this_thread::get_id()) {
            assert(!"Reactor called from arbitrary thread");
            throw std::runtime_error("Reactor called from arbitrary thread");
        }
    }

    void SafeReactor::callAsync(Callback cback)
    {
        // Move, do not copy: the callback may own objects that must be destroyed
        // in the reactor thread (e.g. a WebSocket client handler, see
        // ~WebsocketSession). A copy left in `cback` would drop the last
        // reference in the calling thread if the reactor thread runs and
        // releases its copy before this function returns, and destructors
        // that use io::Reactor::get_Current() then dereference a null reactor.
        std::unique_lock lock(_queueMutex);
        _queue.push(std::move(cback));
        _event->post();
    }
}
