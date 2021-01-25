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

#include "../node.h"
#include "../../mnemonic/mnemonic.h"
#include "../../utility/cli/options.h"
#include "../../core/fly_client.h"
#include "../../core/treasury.h"
#include "../../core/serialization_adapters.h"
#include <boost/core/ignore_unused.hpp>

#define LOG_VERBOSE_ENABLED 0
#include "utility/logger.h"

namespace beam {

uint16_t g_LocalNodePort = 17725;


struct Manager
{
    std::mutex m_Mutex;
    std::condition_variable m_Cmd;
    std::condition_variable m_Response;

    struct PerChain
    {
        Rules m_Rules;
        io::Reactor::Ptr m_pReactor;
        io::AsyncEvent::Ptr m_pEvent;
        std::thread m_Thread;
        Node* m_pNode = nullptr;
        bool m_Stopped = false;
        bool m_Frozen = false;
    };

    bool m_Stop = false;
    bool m_Freeze = false;

    struct Freezer
    {
        Manager& m_This;
        std::unique_lock<std::mutex> m_Lock;

        Freezer(Manager&);
        ~Freezer();
    };

    PerChain m_pC[2];

    ~Manager() { Stop(); }

    bool Start();
    void Stop();

    void RunInThread(uint32_t);
    void RunInThread2(uint32_t);
    void OnEvent(uint32_t);
};

bool Manager::Start()
{
    for (uint32_t i = 0; i < _countof(m_pC); i++)
    {
        auto& c = m_pC[i];

        c.m_pReactor = io::Reactor::create();
        c.m_pEvent = io::AsyncEvent::create(*c.m_pReactor, [this, i]() { OnEvent(i); });

        c.m_Thread = std::thread(&Manager::RunInThread, this, i);
    }

    std::unique_lock<std::mutex> scope(m_Mutex);

    for (uint32_t i = 0; i < _countof(m_pC); i++)
    {
        auto& c = m_pC[i];

        while (true)
        {
            if (c.m_Stopped)
                return false;

            if (c.m_pNode)
                break;

            m_Response.wait(scope);
        }

    }

    return true;

}

void Manager::Stop()
{
    m_Stop = true;

    for (uint32_t i = 0; i < _countof(m_pC); i++)
    {
        auto& c = m_pC[i];

        if (c.m_Thread.joinable())
        {
            c.m_pEvent->post();
            c.m_Thread.join();
        }
    }
}

void Manager::RunInThread(uint32_t i)
{
    auto& c = m_pC[i];

    c.m_Rules.UpdateChecksum();
    Rules::Scope scopeRules(c.m_Rules);

    io::Reactor::Scope scopeReactor(*c.m_pReactor);

    try
    {
        RunInThread2(i);
    }
    catch (const std::exception& e)
    {
        std::cout << e.what() << std::endl;
    }

    std::unique_lock<std::mutex> scope(m_Mutex);
    c.m_Stopped = true;
    c.m_pNode = nullptr;
    m_Response.notify_one();
}

void Manager::RunInThread2(uint32_t i)
{
    auto& c = m_pC[i];

    Node node;

    node.m_Cfg.m_VerificationThreads = -1;
    node.m_Cfg.m_sPathLocal = "node_" + std::to_string(i) + ".db";

    Key::IKdf::Ptr pKdf;

    node.m_Cfg.m_Listen.port(g_LocalNodePort + i);
    node.m_Cfg.m_Listen.ip(INADDR_ANY);

    // disable dandelion (faster tx propagation)
    node.m_Cfg.m_Dandelion.m_AggregationTime_ms = 0;
    node.m_Cfg.m_Dandelion.m_FluffProbability = 0xFFFF;

    node.m_Keys.SetSingleKey(pKdf);
    node.m_Cfg.m_Horizon.SetStdFastSync();
    node.Initialize();

    if (!node.m_PostStartSynced)
    {
        struct MyObserver :public Node::IObserver
        {
            Node& m_Node;
            MyObserver(Node& n) :m_Node(n) {}

            virtual void OnSyncProgress() override
            {
                if (m_Node.m_PostStartSynced)
                    io::Reactor::get_Current().stop();
            }
        };

        MyObserver obs(node);
        Node::IObserver* pObs = &obs;
        TemporarySwap<Node::IObserver*> tsObs(pObs, node.m_Cfg.m_Observer);

        io::Reactor::get_Current().run();

        if (!node.m_PostStartSynced)
            return;
    }

    {
        std::unique_lock<std::mutex> scope(m_Mutex);
        c.m_pNode = &node;
        m_Response.notify_one();
    }

    c.m_pReactor->run();
}

Manager::Freezer::Freezer(Manager& m)
    :m_This(m)
    ,m_Lock(m.m_Mutex)
{
    m_This.m_Freeze = true;

    for (uint32_t i = 0; i < _countof(m_This.m_pC); i++)
        m_This.m_pC[i].m_pEvent->post();

    for (uint32_t i = 0; i < _countof(m_This.m_pC); i++)
    {
        auto& c = m_This.m_pC[i];

        while (c.m_pNode && !c.m_Frozen)
            m_This.m_Response.wait(m_Lock);
    }
}

Manager::Freezer::~Freezer()
{
    m_This.m_Freeze = false;
    m_This.m_Cmd.notify_all();

    for (uint32_t i = 0; i < _countof(m_This.m_pC); i++)
    {
        auto& c = m_This.m_pC[i];

        while (c.m_pNode && c.m_Frozen)
            m_This.m_Response.wait(m_Lock);
    }
}

void Manager::OnEvent(uint32_t i)
{
    auto& c = m_pC[i];

    std::unique_lock<std::mutex> scope(m_Mutex);

    if (m_Freeze)
    {
        c.m_Frozen = true;

        while (true)
        {
            m_Response.notify_one();
            m_Cmd.wait(scope);

            if (!m_Freeze || m_Stop)
                break;
        }

        c.m_Frozen = false;
    }

    if (m_Stop)
        io::Reactor::get_Current().stop();

    m_Response.notify_one();

}

} // namespace beam


int main_Guarded(int argc, char* argv[])
{
    using namespace beam;

    io::Reactor::Ptr pReactor(io::Reactor::create());
    io::Reactor::Scope scope(*pReactor);
    io::Reactor::GracefulIntHandler gih(*pReactor);

    auto logger = beam::Logger::create(LOG_LEVEL_INFO, LOG_LEVEL_INFO);

    Manager man;

    for (uint32_t i = 0; i < _countof(man.m_pC); i++)
    {
        auto& c = man.m_pC[i];
        c.m_Rules = Rules::get(); // default
        c.m_Rules.TreasuryChecksum = i;
        c.m_Rules.UpdateChecksum();
    }

    man.Start();

    {
        Manager::Freezer f(man);
    }

    {
        Manager::Freezer f(man);
    }

    //io::Reactor::get_Current().run();

    return 0;
}

int main(int argc, char* argv[])
{
    int ret = 0;
    try
    {
        ret = main_Guarded(argc, argv);
    }
    catch (const std::exception & e)
    {
        std::cout << e.what() << std::endl;
        return 1;
    }

    return ret;
}
