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
#include "../../utility/logger_checkpoints.h"

#define LOG_VERBOSE_ENABLED 0
#include "utility/logger.h"

namespace beam {


struct Manager
{
    struct Event
    {
        std::mutex m_Mutex;

        struct Client
            :public boost::intrusive::list_base_hook<>
        {
            typedef intrusive::list<Client> List;
            io::Reactor* m_pReactor = nullptr;
        };

        Event::Client::List m_lst;

        struct Context
        {
            Event& m_Event;
            std::unique_lock<std::mutex> m_Lock;

            Context(Event&);

            void Fire();
            bool Wait();
        };

    private:
        void RemoveStrict(Client&);
    };

    struct PerChain
    {
        Rules m_Rules;
        io::Reactor::Ptr m_pReactor;
        io::AsyncEvent::Ptr m_pEvent;
        std::thread m_Thread;
        Node::Config m_Cfg;
        Node* m_pNode = nullptr;
        bool m_Stopped = false;
        bool m_Frozen = false;
        Event m_Event;
    };

    bool m_Stop = false;
    bool m_Freeze = false;

    std::mutex m_CtlMutex;
    std::condition_variable m_CtlVar;

    struct Freezer
    {
        Manager& m_This;

        Freezer(Manager&);
        ~Freezer();

        void Do(bool);
    };

    PerChain m_pC[2];

    ~Manager() { Stop(); }

    bool Start();
    void Stop();

    void RunInThread(uint32_t);
    void RunInThread2(uint32_t);
    void OnEvent(uint32_t);
};


Manager::Event::Context::Context(Event& e)
    :m_Event(e)
    ,m_Lock(e.m_Mutex)
{
}

void Manager::Event::Context::Fire()
{
    while (!m_Event.m_lst.empty())
    {
        Client& c = m_Event.m_lst.front();
        assert(c.m_pReactor);
        c.m_pReactor->stop();

        m_Event.RemoveStrict(c);
    }
}

void Manager::Event::RemoveStrict(Client& c)
{
    assert(c.m_pReactor);
    c.m_pReactor = nullptr;
    m_lst.pop_front();
}

bool Manager::Event::Context::Wait()
{
    io::Reactor& r = io::Reactor::get_Current();

    Client c;
    c.m_pReactor = &r;
    m_Event.m_lst.push_back(c);

    m_Lock.unlock();
    r.run();
    m_Lock.lock();

    if (!c.m_pReactor)
        return true;

    m_Event.RemoveStrict(c);
    r.stop();
    return false;
}

bool Manager::Start()
{
    for (uint32_t i = 0; i < _countof(m_pC); i++)
    {
        auto& c = m_pC[i];

        c.m_pReactor = io::Reactor::create();
        c.m_pEvent = io::AsyncEvent::create(*c.m_pReactor, [this, i]() { OnEvent(i); });

        c.m_Thread = std::thread(&Manager::RunInThread, this, i);
    }

    for (uint32_t i = 0; i < _countof(m_pC); i++)
    {
        auto& c = m_pC[i];

        for (Event::Context ctx(c.m_Event); ; )
        {
            if (c.m_Stopped)
                return false;

            if (c.m_pNode)
                break;

            if (!ctx.Wait())
                return false;
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
//    CHECKPOINT("Pipe", i);

    auto& c = m_pC[i];

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

    Event::Context ctx(c.m_Event);
    c.m_Stopped = true;
    c.m_pNode = nullptr;
    ctx.Fire();
}

void Manager::RunInThread2(uint32_t i)
{
    auto& c = m_pC[i];

    Node node;
    node.m_Cfg = c.m_Cfg; // copy

    //Key::IKdf::Ptr pKdf;

    node.m_Cfg.m_VerificationThreads = -1;
    node.m_Cfg.m_sPathLocal = "node_" + std::to_string(i) + ".db";

    // disable dandelion (faster tx propagation)
    node.m_Cfg.m_Dandelion.m_AggregationTime_ms = 0;
    node.m_Cfg.m_Dandelion.m_FluffProbability = 0xFFFF;

    //node.m_Keys.SetSingleKey(pKdf);
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
        std::cout << "Pipe" << i << " sync complete" << std::endl;

        Event::Context ctx(c.m_Event);
        c.m_pNode = &node;
        ctx.Fire();
    }

    c.m_pReactor->run();
}

Manager::Freezer::Freezer(Manager& m)
    :m_This(m)
{
    Do(true);
}

Manager::Freezer::~Freezer()
{
    Do(false);
}

void Manager::Freezer::Do(bool b)
{
    std::unique_lock<std::mutex> lock(m_This.m_CtlMutex);
    m_This.m_Freeze = b;

    m_This.m_CtlVar.notify_all();

    for (uint32_t i = 0; i < _countof(m_This.m_pC); i++)
    {
        auto& c = m_This.m_pC[i];
        if (b)
            c.m_pEvent->post();

        while ((c.m_Frozen != b) && c.m_pNode)
            m_This.m_CtlVar.wait(lock);
    }
}

void Manager::OnEvent(uint32_t i)
{
    auto& c = m_pC[i];

    std::unique_lock<std::mutex> lock(m_CtlMutex);

    if (m_Freeze)
    {
        c.m_Frozen = true;
        m_CtlVar.notify_all();

        while (m_Freeze)
            m_CtlVar.wait(lock);

        c.m_Frozen = false;
        m_CtlVar.notify_all();
    }

    if (m_Stop)
        io::Reactor::get_Current().stop();
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

        Rules::Scope scopeRules(c.m_Rules);

        auto [options, visibleOptions] = createOptionsDescription(OptionsFlag::NODE_OPTIONS);
        boost::ignore_unused(visibleOptions);
        options.add_options()
            (cli::SEED_PHRASE, po::value<std::string>()->default_value(""), "seed phrase")
            ;

        std::string sCfgPath = std::string("pipe_") + std::to_string(i) + ".cfg";
        po::variables_map vm = getOptions(argc, argv, sCfgPath.c_str(), options);

        c.m_Rules.TreasuryChecksum = i;
        c.m_Rules.UpdateChecksum();

        std::cout << "Pipe" << i << ", " << c.m_Rules.get_SignatureStr() << std::endl;

        std::vector<std::string> vPeers = getCfgPeers(vm);

        for (size_t iPeer = 0; iPeer < vPeers.size(); iPeer++)
        {
            io::Address addr;
            if (addr.resolve(vPeers[iPeer].c_str()))
                c.m_Cfg.m_Connect.push_back(addr);
        }

    }

    std::cout << "waiting both chains to sync..." << std::endl;

    man.Start();

    std::cout << "Initial sync complete" << std::endl;

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
