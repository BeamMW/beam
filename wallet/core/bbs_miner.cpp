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

#include "bbs_miner.h"

namespace beam::wallet
{

void BbsMiner::Stop()
{
    if (!m_vThreads.empty())
    {
        {
            std::unique_lock<std::mutex> scope(m_Mutex);
            m_Shutdown = true;
            m_NewTask.notify_all();
        }

        for (size_t i = 0; i < m_vThreads.size(); i++)
            if (m_vThreads[i].joinable())
                m_vThreads[i].join();

        m_vThreads.clear();
        m_pEvt.reset();
    }
}

void BbsMiner::Thread(uint32_t iThread)
{
    proto::Bbs::NonceType nStep = static_cast<uint32_t>(m_vThreads.size());

    while (true)
    {
        Task::Ptr pTask;

        for (std::unique_lock<std::mutex> scope(m_Mutex); ; m_NewTask.wait(scope))
        {
            if (m_Shutdown)
                return;

            if (!m_Pending.empty())
            {
                pTask = m_Pending.front();
                break;
            }
        }

        Timestamp ts = 0;
        proto::Bbs::NonceType nonce = iThread;
        bool bSuccess = false;

        for (uint32_t i = 0; ; i++)
        {
            if (pTask->m_Done || m_Shutdown)
                break;

            if (!(i & 0xff))
                ts = getTimestamp();

            // attempt to mine it
            ECC::Hash::Value hv;
            ECC::Hash::Processor hp = pTask->m_hpPartial;
            hp
                << ts
                << nonce
                >> hv;

            if (proto::Bbs::IsHashValid(hv))
            {
                bSuccess = true;
                break;
            }

            nonce += nStep;
        }

        if (bSuccess)
        {
            std::unique_lock<std::mutex> scope(m_Mutex);

            if (pTask->m_Done)
                bSuccess = false;
            else
            {
                assert(m_Pending.front() == pTask);

                pTask->m_Msg.m_TimePosted = ts;
                pTask->m_Msg.m_Nonce = nonce;

                pTask->m_Done = true;
                m_Pending.pop_front();
                m_Done.push_back(std::move(pTask));
            }
        }

        if (bSuccess)
            m_pEvt->post();
    }
}
    
}  // namespace beam::wallet
