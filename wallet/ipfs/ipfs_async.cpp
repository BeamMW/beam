// Copyright 2020 The Beam Team
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
#include "ipfs_imp.h"

namespace beam::wallet
{
    PostToReactorThread::Ptr PostToReactorThread::create(beam::io::Reactor::Ptr reactor)
    {
        auto post = new PostToReactorThread();
        auto ptr  = Ptr(post);
        auto weak = std::weak_ptr<PostToReactorThread>(ptr);

        post->_queueEvent = io::AsyncEvent::create(*reactor, [weak]() {
            if (auto ptr = weak.lock())
            {
                ptr->doInReactorThread();
            }
        });

        return ptr;
    }

    PostToReactorThread::~PostToReactorThread()
    {
        assert(_queue.empty());
        assert(_queueEvent.use_count() == 1);
    }

    void PostToReactorThread::post(std::function<void()>&& what)
    {
        std::lock_guard<std::mutex> lock(_queueGuard);
        _queue.push(std::move(what));

        io::AsyncEvent::Trigger trigger(_queueEvent);
        trigger();
    }

    void PostToReactorThread::doInReactorThread()
    {
        auto getAction = [this] (std::function<void ()>& action) -> bool {
            std::lock_guard<std::mutex> lock(_queueGuard);
            if (_queue.empty()) return false;

            action = std::move(_queue.front());
            _queue.pop();
            return true;
        };

        std::function<void ()> action;
        while(getAction(action)) {
            action();
        }
    }
}
