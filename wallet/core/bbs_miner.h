// Copyright 2019 The Beam Team
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

#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "core/ecc_native.h"
#include "core/proto.h"
#include "utility/io/asyncevent.h"

namespace beam::wallet
{

struct BbsMiner
{
    // message mining
    std::vector<MyThread> m_vThreads;
    std::mutex m_Mutex;
    std::condition_variable m_NewTask;

    volatile bool m_Shutdown;
    io::AsyncEvent::Ptr m_pEvt;

    struct Task
    {
        proto::BbsMsg m_Msg;
        ECC::Hash::Processor m_hpPartial;
        volatile bool m_Done;
        uint64_t m_StoredMessageID;

        typedef std::shared_ptr<Task> Ptr;
    };

    typedef std::deque<Task::Ptr> TaskQueue;

    TaskQueue m_Pending;
    TaskQueue m_Done;

    BbsMiner() :m_Shutdown(false) {}
    ~BbsMiner() { Stop(); }

    void Stop();
    void Thread(uint32_t, const Rules&);

};
}  // namespace beam::wallet
