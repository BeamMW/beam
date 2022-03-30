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
#pragma once

#include "utility/thread.h"
#include "utility/io/reactor.h"
#include "utility/io/asyncevent.h"

namespace beam::wallet
{
    class PostToReactorThread: std::enable_shared_from_this<PostToReactorThread>
    {
    public:
        ~PostToReactorThread();

        typedef std::shared_ptr<PostToReactorThread> Ptr;
        static Ptr create(beam::io::Reactor::Ptr reactor);
        void post(std::function<void()>&& what);

    private:
        PostToReactorThread() = default;
        void doInReactorThread();

        std::mutex _queueGuard;
        std::queue<std::function<void ()>> _queue;
        io::AsyncEvent::Ptr _queueEvent;
    };
}