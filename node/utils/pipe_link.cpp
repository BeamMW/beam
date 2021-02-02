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
#include "../../core/block_rw.h"
#include <boost/core/ignore_unused.hpp>
#include "../../utility/logger_checkpoints.h"
#include "../../bvm/bvm2.h"

namespace Shaders {
#define HOST_BUILD
#include "../../bvm/Shaders/common.h"
#include "../../bvm/Shaders/pipe/contract.h"
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

    std::mutex m_OutpMutex;

    struct Printer
    {
        Manager* m_pManager;
        std::stringstream m_Stream;

        Printer(Manager& m) :m_pManager(&m) {}

        Printer(Printer&& p)
        {
            m_pManager = p.m_pManager;
            p.m_pManager = nullptr;
            m_Stream.swap(p.m_Stream);
        }

        template <typename T>
        Printer& operator << (const T& x)
        {
            m_Stream << x;
            return *this;
        }

        ~Printer()
        {
            if (m_pManager) {
                std::unique_lock<std::mutex> lock(m_pManager->m_OutpMutex);
                std::cout << m_Stream.str() << std::endl;
            }
        }
    };

    struct LocalContext
    {
        Manager& m_Manager;
        uint32_t m_iThread;

        LocalContext(Manager& m) :m_Manager(m) {}

        Node m_Node;
        Node m_NodeExt;

        Printer Print();

        struct NodeObserver :public Node::IObserver
        {
            bool m_InitSyncReported = false;

            virtual void OnSyncProgress() override;
            virtual void OnStateChanged() override;
            virtual void OnRolledBack(const Block::SystemState::ID&) override;

            IMPLEMENT_GET_PARENT_OBJ(LocalContext, m_NodeObserver)
        } m_NodeObserver;

        void OnInitSync();
        void OnStateChanged();

        ECC::Hash::Value m_hvCurrentTx = Zero;
        Height m_hCurrentTxH1 = 0;

        bool FindPipeCid(bvm2::ContractID&);
        bool SendContractTx(std::unique_ptr<TxKernelContractControl>&&, const char*, Amount, bool bSpend, ECC::Scalar::Native* pKs, uint32_t nKs);
        bool SendTx(TxKernel::Ptr&&, const char*, Amount, bool bSpend, ECC::Scalar::Native& skKrn);

        typedef std::map<CoinID, Height> CoinMaturityMap;

        CoinMaturityMap m_Coins;
        Height m_hCoinsEvtNext = 0;
        void SyncCoins();

        template <typename TValue, typename TKey>
        TValue* ReadContractEx(const Shaders::ContractID& cid, const TKey& key, NodeDB::Recordset& rs, uint32_t* pExtra = nullptr)
        {
            Shaders::Env::Key_T<TKey> keyEx;
            keyEx.m_Prefix.m_Cid = cid;
            keyEx.m_KeyInContract = key;

            Blob data;
            if (!m_Node.get_Processor().get_DB().ContractDataFind(Blob(&keyEx, sizeof(keyEx)), data, rs))
                return nullptr;
            if (data.n < sizeof(TValue))
                return nullptr;

            if (pExtra)
                *pExtra = data.n - sizeof(TValue);

            return (TValue*) data.p;
        }

        template <typename TValue, typename TKey>
        bool ReadContract(const Shaders::ContractID& cid, const TKey& key, TValue& val)
        {
            NodeDB::Recordset rs;
            auto* pVal = ReadContractEx<TValue>(cid, key, rs);
            if (!pVal)
                return false;

            val = *pVal; // copy
            return true;
        }
    };

    struct PerChain
    {
        std::string m_sPrefix;
        Rules m_Rules;
        Key::IKdf::Ptr m_pKdf;
        Key::IPKdf::Ptr m_pExtOwnerKdf;
        io::Reactor::Ptr m_pReactor;
        io::AsyncEvent::Ptr m_pEvent;
        std::thread m_Thread;
        Node::Config m_Cfg;
        LocalContext* m_pLocal = nullptr;
        bool m_Stopped = false;
        bool m_Frozen = false;
        Event m_Event;
    };

    bool m_AssumeSynced = false;
    bool m_PushDummies = false;
    bool m_Stop = false;
    bool m_Freeze = false;

    std::mutex m_CtlMutex;
    std::condition_variable m_CtlVar;

    struct Freezer
    {
        Manager& m_This;
        std::unique_lock<std::mutex> m_Lock;

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
        Printer(*this) << c.m_sPrefix << e.what();
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

    if (c.m_pExtOwnerKdf)
    {
        lc.m_NodeExt.m_Cfg = c.m_Cfg; // copy

        lc.m_NodeExt.m_Cfg.m_VerificationThreads = -1;
        lc.m_NodeExt.m_Cfg.m_sPathLocal = "node_ext_" + std::to_string(i) + ".db";

        // disable dandelion (faster tx propagation)
        lc.m_NodeExt.m_Cfg.m_Dandelion.m_AggregationTime_ms = 0;
        lc.m_NodeExt.m_Cfg.m_Dandelion.m_FluffProbability = 0xFFFF;

        lc.m_NodeExt.m_Keys.m_pOwner = c.m_pExtOwnerKdf;
        lc.m_NodeExt.m_Cfg.m_Horizon.SetStdFastSync();

        auto port = c.m_Cfg.m_Listen.port();
        if (port)
        {
            lc.m_NodeExt.m_Cfg.m_Listen.port(port + 1000);
            lc.m_NodeExt.m_Cfg.m_Connect.push_back(io::Address(INADDR_ANY, port));
        }

        lc.m_NodeExt.Initialize();
    }

    Node::IObserver* pObs = &lc.m_NodeObserver;
    TemporarySwap<Node::IObserver*> tsObs(pObs, lc.m_Node.m_Cfg.m_Observer);

    if (m_AssumeSynced)
        lc.OnInitSync();

    io::Reactor::get_Current().run();
}

Manager::Printer Manager::LocalContext::Print()
{
    Printer p(m_Manager);
    p << m_Manager.m_pC[m_iThread].m_sPrefix;
    return std::move(p);
}

void Manager::LocalContext::OnInitSync()
{
    assert(!m_NodeObserver.m_InitSyncReported);
    m_NodeObserver.m_InitSyncReported = true;

    Print() << "sync complete";

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

void Manager::LocalContext::NodeObserver::OnRolledBack(const Block::SystemState::ID&)
{
    get_ParentObj().m_Coins.clear();
    get_ParentObj().m_hCoinsEvtNext = 0;
}

Manager::Freezer::Freezer(Manager& m)
    :m_This(m)
    ,m_Lock(m.m_CtlMutex)
{
    Do(true);
}

Manager::Freezer::~Freezer()
{
    Do(false);
}

void Manager::Freezer::Do(bool b)
{
    m_This.m_Freeze = b;

    m_This.m_CtlVar.notify_all();

    for (uint32_t i = 0; i < _countof(m_This.m_pC); i++)
    {
        auto& c = m_This.m_pC[i];
        if (&io::Reactor::get_Current() == c.m_pReactor.get())
        {
            c.m_Frozen = b;
            m_This.m_CtlVar.notify_all();
        }
        else
        {
            if (b)
                c.m_pEvent->post();

            while ((c.m_Frozen != b) && c.m_pLocal)
                m_This.m_CtlVar.wait(m_Lock);
        }
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
    }

    if (m_Stop)
        io::Reactor::get_Current().stop();

    m_CtlVar.notify_all();
}

void Manager::LocalContext::SyncCoins()
{
    auto& proc = m_Node.get_Processor();
    if (m_hCoinsEvtNext > proc.m_Cursor.m_Full.m_Height)
        return;

    struct MyParser
        :public proto::Event::IGroupParser
    {
        LocalContext* m_pThis;

        virtual void OnEventType(proto::Event::Utxo& evt) override
        {
            auto& x = m_pThis->m_Coins;

            if (proto::Event::Flags::Add & evt.m_Flags)
                x[evt.m_Cid] = evt.m_Maturity;
            else
            {
                auto it = x.find(evt.m_Cid);
                if (x.end() != it)
                    x.erase(it);
            }

        }

    } parser;
    parser.m_pThis = this;

    NodeDB::WalkerEvent wlk;
    for (proc.get_DB().EnumEvents(wlk, m_hCoinsEvtNext); wlk.MoveNext(); )
    {
        parser.m_Height = wlk.m_Height;
        parser.ProceedOnce(wlk.m_Body);
    }



    m_hCoinsEvtNext = proc.m_Cursor.m_Full.m_Height + 1;
}

void Manager::LocalContext::OnStateChanged()
{
    const auto& s = m_Node.get_Processor().m_Cursor.m_Full;
    Print() << "Height=" << s.m_Height;

    SyncCoins();

    auto& proc = m_Node.get_Processor();
    auto& db = proc.get_DB();

    if (m_hCurrentTxH1)
    {
        if (db.FindKernel(m_hvCurrentTx) >= Rules::HeightGenesis) {
            Print() << "last tx confirmed";
        } else
        {
            if (s.m_Height < m_hCurrentTxH1)
                return;

            Print() << "last tx timed-out";
        }

        m_hCurrentTxH1 = 0;
    }

    bvm2::ContractID cid;
    if (!FindPipeCid(cid))
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

    Shaders::Pipe::StateIn::Key sik;
    Shaders::Pipe::StateIn si;
    if (!ReadContract(cid, sik, si))
        return;

    Freezer fr(m_Manager);

    auto& cr = m_Manager.m_pC[!m_iThread]; // remote
    if (!cr.m_pLocal)
        return;
    auto& ctxr = *cr.m_pLocal;

    bvm2::ContractID cid2;
    if (!ctxr.FindPipeCid(cid2))
        return;

    if (si.m_cidRemote != cid2)
    {
        if (si.m_cidRemote == Zero)
        {
            Shaders::Pipe::SetRemote arg;
            arg.m_cid = cid2;
            TxKernelContractInvoke::Ptr pKrn(new TxKernelContractInvoke);
            pKrn->m_Cid = cid;
            pKrn->m_iMethod = arg.s_iMethod;
            Blob(&arg, sizeof(arg)).Export(pKrn->m_Args);

            ECC::Scalar::Native sk;
            SendContractTx(std::move(pKrn), "set remote endpoint", 0, true, &sk, 1);
        }

        return;
    }

    if (m_Manager.m_PushDummies && !(s.m_Height % 8))
    {
        struct ArgPlus
        {
            Shaders::Pipe::PushLocal0 m_Arg;
            uint8_t m_pMsg[4];
        } arg;

        ZeroObject(arg.m_pMsg);
        arg.m_Arg.m_MsgSize = sizeof(arg.m_pMsg);
        arg.m_Arg.m_Receiver = Zero;
        TxKernelContractInvoke::Ptr pKrn(new TxKernelContractInvoke);
        pKrn->m_Cid = cid;
        pKrn->m_iMethod = arg.m_Arg.s_iMethod;
        Blob(&arg, sizeof(arg)).Export(pKrn->m_Args);

        ECC::Scalar::Native sk;
        SendContractTx(std::move(pKrn), "push dummy msg", 0, true, &sk, 1);
        return;
    }

    Shaders::Pipe::StateOut::Key sok;
    Shaders::Pipe::StateOut so2;
    if (!ctxr.ReadContract(cid2, sok, so2))
        return;

    uint32_t nCps = so2.m_Checkpoint.m_iIdx;
    if (so2.m_Checkpoint.m_iMsg && so2.IsCheckpointClosed(s.m_Height))
        nCps++;

    if (si.m_Dispute.m_iIdx >= nCps)
    {
        if (so2.m_Checkpoint.m_iMsg && !so2.IsCheckpointClosed(s.m_Height))
            Print() << so2.m_Checkpoint.m_iMsg << " msgs pending. Waiting for full package to assemble";
        return; // up-to-date
    }

    // read the checkpoint details
    Serializer ser;

    {
        Shaders::Env::Key_T<Shaders::Pipe::MsgHdr::KeyOut> mhk;
        mhk.m_Prefix.m_Cid = cid2;
        mhk.m_KeyInContract.m_iCheckpoint_BE = ByteOrder::to_be(si.m_Dispute.m_iIdx);
        mhk.m_KeyInContract.m_iMsg_BE = 0;

        auto mhk2 = mhk;
        mhk2.m_KeyInContract.m_iMsg_BE--;

        NodeDB::WalkerContractData wlk;
        ctxr.m_Node.get_Processor().get_DB().ContractDataEnum(wlk, Blob(&mhk, sizeof(mhk)), Blob(&mhk2, sizeof(mhk2)));
        while (wlk.MoveNext())
        {
            if (wlk.m_Val.n < sizeof(Shaders::Pipe::MsgHdr))
                continue; // ?!

            uint32_t nSize = ByteOrder::to_le(wlk.m_Val.n - (uint32_t) sizeof(Shaders::Pipe::MsgHdr));
            ser.WriteRaw(&nSize, sizeof(nSize));
            ser.WriteRaw(wlk.m_Val.p, wlk.m_Val.n);
        }
    }

    if (!ser.buffer().second)
        return; // ?!

    bool bNewVariant = true;

    ECC::Scalar::Native pSk[2];
    m_Manager.m_pC[m_iThread].m_pKdf->DeriveKey(pSk[0], cid);
    ECC::Point::Native ptN = ECC::Context::get().G * pSk[0];
    ECC::Point ptUser(ptN);

    if (si.m_Dispute.m_Variants)
    {
        // load current win variant
        Shaders::Pipe::Variant::Key vk;
        vk.m_hvVariant = si.m_Dispute.m_hvBestVariant;
        NodeDB::Recordset rs;
        uint32_t nSize;
        auto* pWin = ReadContractEx<Shaders::Pipe::Variant>(cid, vk, rs, &nSize);
        if (!pWin)
            return;

        if (pWin->m_User == ptUser)
        {
            // already winning
            if (si.CanFinalyze(s.m_Height))
            {
                Shaders::Pipe::FinalyzeRemote arg;
                arg.m_DepositStake = 0;

                TxKernelContractInvoke::Ptr pKrn(new TxKernelContractInvoke);
                Blob(&arg, sizeof(arg)).Export(pKrn->m_Args);
                pKrn->m_Cid = cid;
                pKrn->m_iMethod = arg.s_iMethod;

                rs.Reset();
                SendContractTx(std::move(pKrn), "finalyze checkpoint", si.m_Dispute.m_Stake, false, pSk, (uint32_t) _countof(pSk));
            }

            Print() << "waiting for dispute to expire...";

            return;
        }

        if (1 == si.m_Dispute.m_Variants)
        {
            // TODO: evaluate
        }
    }

    ByteBuffer bufArg;
    uint32_t nSizeArg = sizeof(Shaders::Pipe::PushRemote0);
    if (bNewVariant)
        nSizeArg += sizeof(uint32_t) + (uint32_t) ser.buffer().second;
    else
        nSizeArg += sizeof(ECC::Hash::Value); // existing variant ID

    bufArg.resize(nSizeArg);
    auto& arg = *reinterpret_cast<Shaders::Pipe::PushRemote0*>(&bufArg.front());
    arg.m_User = ptUser;
    arg.m_Flags = 0;

    auto* pBuf = &bufArg.front() + sizeof(arg);

    if (bNewVariant)
    {
        arg.m_Flags |= Shaders::Pipe::PushRemote0::Flags::Msgs;

        uint32_t nSizeMsgs = ByteOrder::to_le((uint32_t) ser.buffer().second);

        memcpy(pBuf, &nSizeMsgs, sizeof(nSizeMsgs));
        pBuf += sizeof(nSizeMsgs);

        memcpy(pBuf, ser.buffer().first, nSizeMsgs);
        pBuf += nSizeMsgs;
    }

    TxKernelContractInvoke::Ptr pKrn(new TxKernelContractInvoke);
    pKrn->m_Cid = cid;
    pKrn->m_iMethod = arg.s_iMethod;
    pKrn->m_Args.swap(bufArg);

    SendContractTx(std::move(pKrn), "present checkpoint", bNewVariant ? si.m_Cfg.m_StakeForRemote : 0, true, pSk, (uint32_t)_countof(pSk));
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
    Key::Type outType = Key::Type::Change;

    const Amount& fee = pKrn->m_Fee;
    if (bSpend)
        val += fee;
    else
    {
        if (val >= fee)
        {
            val -= fee;
            outType = Key::Type::Regular;
        }
        else
        {
            val = fee - val;
            bSpend = true;
        }
    }

    m_hvCurrentTx = pKrn->m_Internal.m_ID;
    Height h1 = pKrn->m_Height.m_Max;

    Transaction::Ptr pTx = std::make_shared<Transaction>();
    pTx->m_vKernels.push_back(std::move(pKrn));

    Key::IKdf::Ptr& pKdf = m_Manager.m_pC[m_iThread].m_pKdf;

    if (bSpend)
    {
        skKrn = -skKrn;

        for (auto it = m_Coins.begin(); val; it++)
        {
            if (m_Coins.end() == it)
            {
                Print() << "Send tx failed (" << sz << "), Insuffucient funds, missing " << val;
                return false;
            }

            auto& cid = it->first;
            if (cid.m_AssetID || !cid.m_Value)
                continue;

            if (it->second >= h1)
                continue;

            auto& pInp = pTx->m_vInputs.emplace_back();
            pInp.reset(new Input);


            ECC::Scalar::Native sk;
            CoinID::Worker(cid).Create(sk, pInp->m_Commitment, *pKdf);
            skKrn += sk;

            if (val <= cid.m_Value)
            {
                val = cid.m_Value - val;
                break;
            }

            val -= cid.m_Value;
        }

        skKrn = -skKrn;
    }

    if (val)
    {
        auto& pOutp = pTx->m_vOutputs.emplace_back();
        pOutp.reset(new Output);

        CoinID cid(Zero);
        cid.m_Value = val;
        ECC::GenRandom(&cid.m_Idx, sizeof(cid.m_Idx));
        cid.m_Type = outType;

        ECC::Scalar::Native sk;
        pOutp->Create(h1, sk, *cid.get_ChildKdf(pKdf), cid, *pKdf);

        skKrn += sk;
    }

    pTx->m_Offset = -skKrn;

    pTx->Normalize();

    uint8_t nCode = m_Node.OnTransaction(std::move(pTx), nullptr, true);
    if (proto::TxStatus::Ok != nCode)
    {
        Print() << "Send tx failed (" << sz << "), Status=" << static_cast<uint32_t>(nCode);
        return false;
    }


    Print() << "Sent tx: (" << sz << ")";

    m_hCurrentTxH1 = h1;
    return true;
}

bool Manager::LocalContext::FindPipeCid(bvm2::ContractID& cid)
{
#pragma pack (push, 1)
    struct SidCid
    {
        Shaders::ShaderID m_Sid;
        Shaders::ContractID m_Cid;
    };
#pragma pack (pop)

    typedef Shaders::Env::Key_T<SidCid> Key;

    Key sck;
    ZeroObject(sck);
    sck.m_Prefix.m_Tag = Shaders::KeyTag::SidCid;
    sck.m_KeyInContract.m_Sid = m_Manager.m_sidPipe;

    Key sck2 = sck;
    sck2.m_KeyInContract.m_Cid.Inv();

    NodeDB::WalkerContractData wlk;
    m_Node.get_Processor().get_DB().ContractDataEnum(wlk, Blob(&sck, sizeof(sck)), Blob(&sck2, sizeof(sck2)));

    while (wlk.MoveNext())
    {
        if (wlk.m_Key.n == sizeof(sck))
        {
            cid = ((Key*) wlk.m_Key.p)->m_KeyInContract.m_Cid;
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
    man.m_AssumeSynced = true;

    {
        std::FStream fs;
        fs.Open("pipe/contract.wasm", true, true);

        man.m_shaderPipe.resize(static_cast<size_t>(fs.get_Remaining()));
        if (!man.m_shaderPipe.empty())
            fs.read(&man.m_shaderPipe.front(), man.m_shaderPipe.size());

        bvm2::Processor::Compile(man.m_shaderPipe, man.m_shaderPipe, bvm2::Processor::Kind::Contract);
        bvm2::get_ShaderID(man.m_sidPipe, man.m_shaderPipe);
    }

    man.m_pC[0].m_sPrefix = "<mainchain> ";
    man.m_pC[1].m_sPrefix = "<sidechain> ";

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

        if (c.m_Rules.FakePoW)
        {
            //c.m_Rules.TreasuryChecksum = Zero;
            //c.m_Rules.Prehistoric = i;

            //c.m_Rules.pForks[1].m_Height = 1;
            //c.m_Rules.pForks[2].m_Height = 1;
            //c.m_Rules.pForks[3].m_Height = 1;

            //c.m_Rules.FakePoW = true;

            auto pKdf = std::make_shared<ECC::HKdf>();
            pKdf->Generate(c.m_Rules.Prehistoric);

            c.m_pKdf = std::move(pKdf);

            c.m_Cfg.m_MiningThreads = 1;
            c.m_Cfg.m_TestMode.m_FakePowSolveTime_ms = 2000;
        }

        c.m_Rules.UpdateChecksum();

        Manager::Printer(man) << c.m_sPrefix << c.m_Rules.get_SignatureStr();

        std::vector<std::string> vPeers = getCfgPeers(vm);

        for (size_t iPeer = 0; iPeer < vPeers.size(); iPeer++)
        {
            io::Address addr;
            if (addr.resolve(vPeers[iPeer].c_str()))
                c.m_Cfg.m_Connect.push_back(addr);
        }

        auto t = vm[cli::TREASURY_BLOCK];
        if (!t.empty())
        {
            std::FStream f;
            if (f.Open(t.as<std::string>().c_str(), true))
            {
                size_t nSize = static_cast<size_t>(f.get_Remaining());
                if (nSize)
                {
                    c.m_Cfg.m_Treasury.resize(nSize);
                    f.read(&c.m_Cfg.m_Treasury.front(), nSize);
                }
            }
        }

        t = vm[cli::PORT];
        if (!t.empty())
        {
            c.m_Cfg.m_Listen.port(t.as<uint16_t>());
            c.m_Cfg.m_Listen.ip(INADDR_ANY);
        }

        t = vm[cli::OWNER_KEY];
        if (!t.empty())
        {
            KeyString ks;
            ks.SetPassword(Blob("1", 1));
            ks.m_sRes = t.as<std::string>();

            auto pKdf = std::make_shared<ECC::HKdfPub>();
            if (!ks.Import(*pKdf))
                throw std::runtime_error("owner key import failed");

            c.m_pExtOwnerKdf = std::move(pKdf);
        }
    }

    Manager::Printer(man) << "waiting both chains to sync...";

    man.Start();

    Manager::Printer(man) << "Initial sync complete";

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
