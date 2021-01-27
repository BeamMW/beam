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
#include "../../bvm/bvm2.h"

namespace Shaders {
#define HOST_BUILD
#include "../../bvm/Shaders/common.h"
#include "../../bvm/Shaders/pipe/contract.h"

#pragma pack (push, 1)
namespace Key {
    struct Prefix
    {
        ContractID m_Cid;
        uint8_t m_Tag;
    };

    struct SidCid
    {
        Prefix m_Prefix;
        ShaderID m_Sid;
        ContractID m_Cid;
    };
} // namespace Key
#pragma pack (pop)

} // namespace Shaders

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

    struct LocalContext
    {
        Manager& m_Manager;
        uint32_t m_iThread;

        LocalContext(Manager& m) :m_Manager(m) {}

        Node m_Node;

        struct NodeObserver :public Node::IObserver
        {
            bool m_InitSyncReported = false;

            virtual void OnSyncProgress() override;
            virtual void OnStateChanged() override;

            IMPLEMENT_GET_PARENT_OBJ(LocalContext, m_NodeObserver)
        } m_NodeObserver;

        void OnInitSync();
        void OnStateChanged();

        ECC::Hash::Value m_hvCurrentTx = Zero;
        Height m_hCurrentTxH1 = 0;

        bool FindPipeCid(bvm2::ContractID&);
        bool SendContractTx(std::unique_ptr<TxKernelContractControl>&&, const char*, Amount, bool bSpend, ECC::Scalar::Native* pKs, uint32_t nKs);
        bool SendTx(TxKernel::Ptr&&, const char*, Amount, bool bSpend, ECC::Scalar::Native& skKrn);
    };

    struct PerChain
    {
        Rules m_Rules;
        Key::IKdf::Ptr m_pKdf;
        io::Reactor::Ptr m_pReactor;
        io::AsyncEvent::Ptr m_pEvent;
        std::thread m_Thread;
        Node::Config m_Cfg;
        LocalContext* m_pLocal = nullptr;
        bool m_Stopped = false;
        bool m_Frozen = false;
        Event m_Event;
    };

    bool m_DemoMode = false;
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

    ByteBuffer m_shaderPipe;
    bvm2::ShaderID m_sidPipe;

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

            if (c.m_pLocal)
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
    c.m_pLocal = nullptr;
    ctx.Fire();
}

void Manager::RunInThread2(uint32_t i)
{
    auto& c = m_pC[i];

    LocalContext lc(*this);
    lc.m_iThread = i;

    lc.m_Node.m_Cfg = c.m_Cfg; // copy

    lc.m_Node.m_Cfg.m_VerificationThreads = -1;
    lc.m_Node.m_Cfg.m_sPathLocal = "node_" + std::to_string(i) + ".db";

    // disable dandelion (faster tx propagation)
    lc.m_Node.m_Cfg.m_Dandelion.m_AggregationTime_ms = 0;
    lc.m_Node.m_Cfg.m_Dandelion.m_FluffProbability = 0xFFFF;

    lc.m_Node.m_Keys.SetSingleKey(c.m_pKdf);
    lc.m_Node.m_Cfg.m_Horizon.SetStdFastSync();
    lc.m_Node.Initialize();

    Node::IObserver* pObs = &lc.m_NodeObserver;
    TemporarySwap<Node::IObserver*> tsObs(pObs, lc.m_Node.m_Cfg.m_Observer);

    if (m_DemoMode)
        lc.OnInitSync();

    io::Reactor::get_Current().run();
}

void Manager::LocalContext::OnInitSync()
{
    assert(!m_NodeObserver.m_InitSyncReported);
    m_NodeObserver.m_InitSyncReported = true;

    std::cout << "Pipe" << m_iThread << " sync complete" << std::endl;

    auto& c = m_Manager.m_pC[m_iThread];
    Event::Context ctx(c.m_Event);
    c.m_pLocal = this;
    ctx.Fire();
}

void Manager::LocalContext::NodeObserver::OnSyncProgress()
{
    if (get_ParentObj().m_Node.m_PostStartSynced && !m_InitSyncReported)
        get_ParentObj().OnInitSync();
}

void Manager::LocalContext::NodeObserver::OnStateChanged()
{
    get_ParentObj().OnStateChanged();
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

        while ((c.m_Frozen != b) && c.m_pLocal)
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

void Manager::LocalContext::OnStateChanged()
{
//    auto& c = m_pC[i];

    std::cout << "Pipe" << m_iThread << " Height=" << m_Node.get_Processor().m_Cursor.m_Full.m_Height << std::endl;

    auto& proc = m_Node.get_Processor();
    auto& db = proc.get_DB();

    if (m_hCurrentTxH1)
    {
        if (db.FindKernel(m_hvCurrentTx) >= Rules::HeightGenesis) {
            std::cout << "last tx confirmed" << std::endl;
        } else
        {
            if (proc.m_Cursor.m_Full.m_Height < m_hCurrentTxH1)
                return;

            std::cout << "last tx timed-out" << std::endl;
        }

        m_hCurrentTxH1 = 0;
    }

    bvm2::ContractID cid;
    if (FindPipeCid(cid))
    {
    }
    else
    {
        const Rules& rRemote = m_Manager.m_pC[!m_iThread].m_Rules;

        Shaders::Pipe::Create arg;
        arg.m_Cfg.m_In.m_FakePoW = rRemote.FakePoW;
        arg.m_Cfg.m_In.m_RulesRemote = rRemote.get_LastFork().m_Hash;
        arg.m_Cfg.m_In.m_ComissionPerMsg = Rules::Coin; // 1 beam
        arg.m_Cfg.m_In.m_StakeForRemote = Rules::Coin * 100;
        arg.m_Cfg.m_In.m_hDisputePeriod = 10;
        arg.m_Cfg.m_In.m_hContenderWaitPeriod = 5;
        arg.m_Cfg.m_Out.m_CheckpointMaxDH = 10;
        arg.m_Cfg.m_Out.m_CheckpointMaxMsgs = 64;

        TxKernelContractCreate::Ptr pKrn(new TxKernelContractCreate);
        pKrn->m_Data = m_Manager.m_shaderPipe;
        Blob(&arg, sizeof(arg)).Export(pKrn->m_Args);

        ECC::Scalar::Native sk;
        SendContractTx(std::move(pKrn), "create pipe", 0, true, &sk, 1);
    }
}

bool Manager::LocalContext::SendContractTx(std::unique_ptr<TxKernelContractControl>&& pKrn, const char* sz, Amount val, bool bSpend, ECC::Scalar::Native* pKs, uint32_t nKs)
{
    pKrn->m_Fee = Rules::Coin / 50; // 2 cents
    pKrn->m_Height.m_Min = m_Node.get_Processor().m_Cursor.m_Full.m_Height;
    pKrn->m_Height.m_Max = pKrn->m_Height.m_Min + 10;

    auto& skKrn = pKs[nKs - 1];
    skKrn.GenRandomNnz();

    ECC::Point::Native ptFunds;
    if (val)
    {
        ptFunds = ECC::Context::get().H * val;
        if (!bSpend)
            ptFunds = -ptFunds;
    }

    pKrn->Sign(pKs, nKs, ptFunds);
    return SendTx(std::move(pKrn), sz, val, bSpend, skKrn);
}

bool Manager::LocalContext::SendTx(TxKernel::Ptr&& pKrn, const char* sz, Amount val, bool bSpend, ECC::Scalar::Native& skKrn)
{
    const Amount& fee = pKrn->m_Fee;
    if (bSpend)
        val += fee;
    else
    {
        if (val >= fee)
            val -= fee;
        else
        {
            val = fee - val;
            bSpend = true;
        }
    }

    ECC::Hash::Value hv = pKrn->m_Internal.m_ID;
    Height h1 = pKrn->m_Height.m_Max;

    Transaction::Ptr pTx = std::make_shared<Transaction>();
    pTx->m_vKernels.push_back(std::move(pKrn));

    // TODO: select coins

    pTx->Normalize();

    uint8_t nCode = m_Node.OnTransaction(std::move(pTx), nullptr, true);
    if (proto::TxStatus::Ok != nCode)
    {
        std::cout << "Send tx failed (" << sz << "), Status=" << static_cast<uint32_t>(nCode) << std::endl;
        return false;
    }


    std::cout << "Sent tx: (" << sz << ")" << std::endl;

    m_hvCurrentTx = hv;
    m_hCurrentTxH1 = h1;
    return true;
}

bool Manager::LocalContext::FindPipeCid(bvm2::ContractID& cid)
{
    Shaders::Key::SidCid sck;
    ZeroObject(sck);
    sck.m_Prefix.m_Tag = Shaders::KeyTag::SidCid;
    sck.m_Sid = m_Manager.m_sidPipe;

    Shaders::Key::SidCid sck2 = sck;
    sck2.m_Cid.Inv();

    NodeDB::WalkerContractData wlk;
    m_Node.get_Processor().get_DB().ContractDataEnum(wlk, Blob(&sck, sizeof(sck)), Blob(&sck2, sizeof(sck2)));

    while (wlk.MoveNext())
    {
        if (wlk.m_Key.n == sizeof(sck))
        {
            cid = ((Shaders::Key::SidCid*) wlk.m_Key.p)->m_Cid;
            return true;
        }
    }

    return false;
}

} // namespace beam


int main_Guarded(int argc, char* argv[])
{
    using namespace beam;

    io::Reactor::Ptr pReactor(io::Reactor::create());
    io::Reactor::Scope scope(*pReactor);
    io::Reactor::GracefulIntHandler gih(*pReactor);

    //auto logger = beam::Logger::create(LOG_LEVEL_INFO, LOG_LEVEL_INFO);

    Manager man;
    man.m_DemoMode = true;

    {
        std::FStream fs;
        fs.Open("pipe/contract.wasm", true, true);

        man.m_shaderPipe.resize(static_cast<size_t>(fs.get_Remaining()));
        if (!man.m_shaderPipe.empty())
            fs.read(&man.m_shaderPipe.front(), man.m_shaderPipe.size());

        bvm2::Processor::Compile(man.m_shaderPipe, man.m_shaderPipe, bvm2::Processor::Kind::Contract);
        bvm2::get_ShaderID(man.m_sidPipe, man.m_shaderPipe);
    }


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

        if (man.m_DemoMode)
        {
            c.m_Rules.TreasuryChecksum = Zero;
            c.m_Rules.Prehistoric = i;

            c.m_Rules.pForks[1].m_Height = 1;
            c.m_Rules.pForks[2].m_Height = 1;
            c.m_Rules.pForks[3].m_Height = 1;

            c.m_Rules.FakePoW = true;

            auto pKdf = std::make_shared<ECC::HKdf>();
            pKdf->Generate(c.m_Rules.Prehistoric);

            c.m_pKdf = std::move(pKdf);

            c.m_Cfg.m_MiningThreads = 1;
            c.m_Cfg.m_TestMode.m_FakePowSolveTime_ms = 2000;
        }

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

    //{
    //    Manager::Freezer f(man);
    //}

    //{
    //    Manager::Freezer f(man);
    //}

    io::Reactor::get_Current().run();

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
