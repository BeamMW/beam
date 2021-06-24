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

#include "wallet/laser/connection.h"
#include "wallet/core/wallet_request_bbs_msg.h"
#include "utility/logger.h"

namespace beam::wallet::laser
{
Connection::Connection(const FlyClient::NetworkStd::Ptr& net,  bool mineOutgoing)
    : m_pNet(net)
    , m_MineOutgoing(mineOutgoing)
{
}

Connection::~Connection()
{
    m_Miner.Stop();
}

void Connection::Connect()
{
    m_pNet->Connect();
}

void Connection::Disconnect()
{
    m_pNet->Disconnect();
}

void Connection::BbsSubscribe(
        BbsChannel ch,
        Timestamp timestamp,
        FlyClient::IBbsReceiver* receiver)
{
    m_pNet->BbsSubscribe(ch, timestamp, receiver);
}

void Connection::PostRequestInternal(FlyClient::Request& r)
{
    if (FlyClient::Request::Type::Transaction == r.get_Type())
        LOG_DEBUG() << "### Broadcasting transaction ###";

    if (FlyClient::Request::Type::BbsMsg == r.get_Type())
    {
        LOG_DEBUG()  << "### Bbs mesage out ###";
        if (m_MineOutgoing && !Rules::get().FakePoW)
        {
            try
            {
                MineBbsRequest(dynamic_cast<FlyClient::RequestBbsMsg&>(r));
            }
            catch(const std::bad_cast&)
            {
                LOG_ERROR()  << "### Bbs mesage out  ERROR ###";
            }
            return;
        }
    } 

    m_pNet->PostRequestInternal(r);
}

void Connection::OnMined()
{
    while (true)
    {
        BbsMiner::Task::Ptr pTask;
        {
            std::unique_lock<std::mutex> scope(m_Miner.m_Mutex);

            if (!m_Miner.m_Done.empty())
            {
                pTask = std::move(m_Miner.m_Done.front());
                m_Miner.m_Done.pop_front();
            }
        }

        if (!pTask)
            break;

        auto it = m_handlers.find(pTask);
        if (it != m_handlers.end())
        {
            WalletRequestBbsMsg::Ptr pReq(new WalletRequestBbsMsg);
            LOG_DEBUG() << "OnMined() diff: "
                        << getTimestamp() - pTask->m_Msg.m_TimePosted;
            pReq->m_Msg = std::move(pTask->m_Msg);
            pReq->m_pTrg = it->second;

            m_pNet->PostRequestInternal(*pReq);

            m_handlers.erase(it);
        }
    }
}

void Connection::MineBbsRequest(FlyClient::RequestBbsMsg& r)
{
    BbsMiner::Task::Ptr pTask = std::make_shared<BbsMiner::Task>();
    pTask->m_Msg = r.m_Msg;
    pTask->m_Done = false;

    proto::Bbs::get_HashPartial(pTask->m_hpPartial, pTask->m_Msg);

    if (!m_Miner.m_pEvt)
    {
        m_Miner.m_pEvt = io::AsyncEvent::create(
            io::Reactor::get_Current(),
            [this] () { OnMined(); });
        m_Miner.m_Shutdown = false;

        uint32_t nThreads = std::thread::hardware_concurrency();
        nThreads = (nThreads > 1) ? (nThreads - 1) : 1; // leave at least 1 vacant core for other things
        m_Miner.m_vThreads.resize(nThreads);

        for (uint32_t i = 0; i < nThreads; i++)
            m_Miner.m_vThreads[i] = MyThread(&BbsMiner::Thread, &m_Miner, i, Rules::get());
    }

    std::unique_lock<std::mutex> scope(m_Miner.m_Mutex);

    m_handlers[pTask] = r.m_pTrg;
    m_Miner.m_Pending.push_back(std::move(pTask));
    m_Miner.m_NewTask.notify_all();
}

}  // namespace beam::wallet::laser
