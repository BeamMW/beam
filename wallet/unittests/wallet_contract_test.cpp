// Copyright 2018-2021 The Beam Team
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

#include "test_helpers.h"
#include "wallet_test_node.h"
#include "utility/logger.h"
#include <boost/filesystem.hpp>
#include "wallet/core/wallet.h"
#include "wallet/core/wallet_network.h"
#include "bvm/bvm2.h"
#include "bvm/ManagerStd.h"
#include "core/fly_client.h"
#include <memory>
#include <inttypes.h>
#undef small
#include "3rdparty/libelfin/dwarf/dwarf++.hh"

#undef TRUE
#include "dap/io.h"
#include "dap/protocol.h"
#include "dap/session.h"

#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <unordered_set>
#include <filesystem>
#include <boost/functional/hash.hpp>
#include <stack>

#ifdef _MSC_VER
#define OS_WINDOWS 1
#endif

#ifdef OS_WINDOWS
#include <fcntl.h>  // _O_BINARY
#include <io.h>     // _setmode
#endif              // OS_WINDOWS

WALLET_TEST_INIT

#include "wallet_test_environment.cpp"

using namespace beam;
using namespace beam::wallet;

void DoDebug(const Wasm::Processor& proc);

namespace
{
    std::string GetCanonicalPath(const std::string& p)
    {
        return std::filesystem::canonical(p).string();
    }

    class WasmLoader : public dwarf::loader
    {
    public:
        WasmLoader(const beam::Wasm::Compiler& c)
            : m_Compiler(c)
        {
        }

        const void* load(dwarf::section_type section, size_t* size_out)
        {
            const auto& name = dwarf::elf::section_type_to_name(section);
            const auto& sections = m_Compiler.m_CustomSections;
            auto it = std::find_if(sections.begin(), sections.end(),
                [&](const auto& s)
            {
                return s.m_Name == name;
            });

            if (it == sections.end())
                return nullptr;

            *size_out = it->size();
            return it->data();
        }
    private:
        beam::Wasm::Compiler m_Compiler;
    };

    struct MyNetwork : proto::FlyClient::NetworkStd
    {
        using NetworkStd::NetworkStd;

        void OnLoginSetup(proto::Login& msg) override
        {
            msg.m_Flags &= ~proto::LoginFlags::MiningFinalization;
        }
    };

    struct MyWalletObserver : wallet::IWalletObserver
    {
        void onSyncProgress(int done, int total) override
        {
            if (done == total && total == 0)
            {
                io::Reactor::get_Current().stop();
            }
        }
        void onOwnedNode(const PeerID& id, bool connected) override {}
    };

    struct MyManager
        : public bvm2::ManagerStd
    {
        bool m_Done = false;
        bool m_Err = false;
        bool m_Async = false;

        void OnDone(const std::exception* pExc) override
        {
            m_Done = true;
            m_Err = !!pExc;

            if (pExc)
                std::cout << "Shader exec error: " << pExc->what() << std::endl;
            else
                std::cout << "Shader output: " << m_Out.str() << std::endl;

            if (m_Async)
                io::Reactor::get_Current().stop();
        }

        static ByteBuffer Load(const char* sz)
        {
            std::FStream fs;
            fs.Open(sz, true, true);

            ByteBuffer res;
            res.resize(static_cast<size_t>(fs.get_Remaining()));
            if (!res.empty())
                fs.read(&res.front(), res.size());

            return res;
        }

        static void Compile(ByteBuffer& res, const char* sz, Kind kind)
        {
            res = Load(sz);

            bvm2::Processor::Compile(res, res, kind);
        }
    };

    bool InvokeShader(Wallet::Ptr wallet
        , IWalletDB::Ptr walletDB
        , const std::string& appShader
        , const std::string& contractShader
        , std::string args)
    {
        MyManager man;
        man.m_pPKdf = walletDB->get_OwnerKdf();
        man.m_pNetwork = wallet->GetNodeEndpoint();
        man.m_pHist = &walletDB->get_History();

        if (appShader.empty())
            throw std::runtime_error("shader file not specified");

        MyManager::Compile(man.m_BodyManager, appShader.c_str(), MyManager::Kind::Manager);

        if (!contractShader.empty())
        {
            auto buffer = MyManager::Load(contractShader.c_str());
            bvm2::Processor::Compile(man.m_BodyContract, buffer, MyManager::Kind::Contract);
        }

        if (!args.empty())
            man.AddArgs(&args.front());

        std::cout << "Executing shader..." << std::endl;

        man.StartRun(man.m_Args.empty() ? 0 : 1); // scheme if no args

        if (!man.m_Done)
        {
            man.m_Async = true;
            io::Reactor::get_Current().run();

            if (!man.m_Done)
            {
                // abort, propagate it
                io::Reactor::get_Current().stop();
                return false;
            }
        }

        if (man.m_Err || man.m_vInvokeData.empty())
            return false;

        wallet->StartTransaction(
            CreateTransactionParameters(TxType::Contract)
            .SetParameter(TxParameterID::ContractDataPacked, man.m_vInvokeData)
        );
        return true;
    }

    class MyDebugger
    {
    public:
        MyDebugger(const std::string& contractPath)
        {
            // load symbols
            m_Buffer = MyManager::Load(contractPath.c_str());
            ByteBuffer res;
            bvm2::Processor::Compile(m_Compiler, res, m_Buffer, bvm2::Processor::Kind::Contract);
            m_DebugInfo = std::make_unique<dwarf::dwarf>(std::make_shared<WasmLoader>(m_Compiler));
        }

        virtual void DoDebug(const Wasm::Processor& proc)
        {

        }

    protected:

        bool FindPC(const dwarf::die& d, dwarf::taddr pc, std::vector<dwarf::die>* stack)
        {
            using namespace dwarf;

            // Scan children first to find most specific DIE
            bool found = false;
            for (auto& child : d) {
                found = FindPC(child, pc, stack);
                if (found)
                    break;
            }
            switch (d.tag) {
                //case DW_TAG::variable:
                //case DW_TAG::formal_parameter:
            case DW_TAG::subprogram:
            case DW_TAG::inlined_subroutine:
                try {
                    if (found || die_pc_range(d).contains(pc)) {
                        found = true;
                        stack->push_back(d);
                    }
                }
                catch (out_of_range&) {
                }
                catch (value_type_mismatch&) {
                }
                break;
            default:
                break;
            }
            return found;
        }

        void DumpDIE(const dwarf::die& node, int intent = 0)
        {
            //for (int i = 0; i < intent; ++i)
            //	printf("      ");
            //printf("<%" PRIx64 "> %s\n",
            //	node.get_section_offset(),
            //	to_string(node.tag).c_str());
            //for (auto& attr : node.attributes())
            //{
            //	for (int i = 0; i < intent; ++i)
            //		printf("      ");

            //	printf("      %s %s\n",
            //		to_string(attr.first).c_str(),
            //		to_string(attr.second).c_str());
            //}


            //for (const auto& child : node)
            //	DumpDIE(child, intent + 1);

        }

        bool PrintPC(dwarf::taddr pc, std::vector<dwarf::die>& stack)
        {
            for (auto& cu : m_DebugInfo->compilation_units())
            {
                if (die_pc_range(cu.root()).contains(pc)) {
                    // Map PC to a line
                    auto& lt = cu.get_line_table();
                    auto it = lt.find_address(pc);
                    if (it == lt.end())
                        return false;
                    else
                    {
                        if (!m_CurrentLine || *m_CurrentLine != it)
                        {
                            if (m_CurrentLine && (*m_CurrentLine)->is_stmt)
                            {
                                m_PrevLine = m_CurrentLine;
                            }
                            m_CurrentLine = std::make_shared<dwarf::line_table::iterator>(it);
                        }
                        else
                        {
                            return false;
                        }
                    }


                    // Map PC to an object
                    // XXX Index/helper/something for looking up PCs
                    // XXX DW_AT_specification and DW_AT_abstract_origin
                    //vector<dwarf::die> stack;

                    if (FindPC(cu.root(), pc, &stack)) {
                        //bool first = true;
                        for (auto& d : stack) {
                            //if (!first)
                            //	printf("\nInlined in:\n");
                            //first = false;
                            DumpDIE(d);
                        }
                        return true;
                    }
                    break;
                }
            }
            return false;
        }

    protected:
        ByteBuffer m_Buffer;
        Wasm::Compiler m_Compiler;
        std::unique_ptr<dwarf::dwarf> m_DebugInfo;
        std::shared_ptr<dwarf::line_table::iterator> m_CurrentLine;
        std::shared_ptr<dwarf::line_table::iterator> m_PrevLine;
    };

    class TestLocalNode : private Node::IObserver
    {
    public:
        TestLocalNode(const ByteBuffer& binaryTreasury
            , Key::IKdf::Ptr pKdf
            , uint16_t port = 32125
            , const std::string& path = "c:\\Data\\Projects\\Beam\\beam-ee5.2RC\\wallet\\unittests\\mytest.db"
            , const std::vector<io::Address>& peers = {}
        )
        {
            m_Node.m_Cfg.m_Treasury = binaryTreasury;
            ECC::Hash::Processor() << Blob(m_Node.m_Cfg.m_Treasury) >> Rules::get().TreasuryChecksum;

            boost::filesystem::remove(path);
            m_Node.m_Cfg.m_sPathLocal = path;
            m_Node.m_Cfg.m_Listen.port(port);
            m_Node.m_Cfg.m_Listen.ip(INADDR_ANY);
            m_Node.m_Cfg.m_MiningThreads = 1;
            m_Node.m_Cfg.m_VerificationThreads = 1;
            m_Node.m_Cfg.m_TestMode.m_FakePowSolveTime_ms = 0;
            m_Node.m_Cfg.m_Connect = peers;

            m_Node.m_Cfg.m_Dandelion.m_AggregationTime_ms = 0;
            m_Node.m_Cfg.m_Dandelion.m_OutputsMin = 0;

            if (pKdf)
            {
                m_Node.m_Keys.SetSingleKey(pKdf);
            }

            m_Node.m_Cfg.m_Observer = this;
            m_Node.Initialize();
            m_Node.m_PostStartSynced = true;
        }

        void GenerateBlocks(uint32_t n, bool stopAfter = true)
        {
            m_StopAfter = stopAfter;
            m_BlocksToGenerate = n;

            GenerateIfInSync();
            io::Reactor::get_Current().run();
        }

        void SetDebugger(MyDebugger* d)
        {
            m_Debugger = d;
        }

    private:

        void GenerateIfInSync()
        {
            if (m_InSync)
            {
                m_Node.GenerateFakeBlocks(m_BlocksToGenerate);
                m_PendingBlocks = false;
            }
            else
            {
                m_PendingBlocks = true;
            }
        }

        void OnRolledBack(const Block::SystemState::ID& id) override
        {

        }
        void OnSyncProgress() override
        {
            UpdateSyncStatus();
            if (m_PendingBlocks)
            {
                GenerateIfInSync();
            }
        }

        void OnStateChanged() override
        {
            if (!m_InSync || m_BlocksToGenerate == 0)
                return;

            if (--m_BlocksToGenerate == 0 && m_StopAfter)
                io::Reactor::get_Current().stop();
        }

        void OnDebugHook(const Wasm::Processor& proc) override
        {
            if (m_Debugger)
                m_Debugger->DoDebug(proc);
        }

        void UpdateSyncStatus()
        {
            Node::SyncStatus s = m_Node.m_SyncStatus;

            if (MaxHeight == m_Done0)
                m_Done0 = s.m_Done;
            s.ToRelative(m_Done0);

            // make sure no overflow during conversion from SyncStatus to int,int.
            const auto threshold = static_cast<unsigned int>(std::numeric_limits<int>::max());
            while (s.m_Total > threshold)
            {
                s.m_Total >>= 1;
                s.m_Done >>= 1;
            }

            m_InSync = (s.m_Done == s.m_Total);

        }

    private:
        Node m_Node;
        uint32_t m_BlocksToGenerate = 0;
        Height m_Done0 = MaxHeight;
        bool m_InSync = true;
        bool m_PendingBlocks = false;
        bool m_StopAfter = false;
        MyDebugger* m_Debugger = nullptr;
    };
}

void TestNode()
{
    auto walletDB = createWalletDB("wallet.db", true);
    auto binaryTreasury = createTreasury(walletDB, {});
    auto walletDB2 = createWalletDB("wallet2.db", true);

    TestLocalNode nodeA{ binaryTreasury, walletDB->get_MasterKdf() };
    TestLocalNode nodeB{ binaryTreasury, walletDB2->get_MasterKdf(), 32126, "mytest2.db", {io::Address::localhost().port(32125)} };

    nodeA.GenerateBlocks(10);
    nodeB.GenerateBlocks(15);

    auto checkCoins = [](IWalletDB::Ptr walletDB, size_t count, uint16_t port)
    {
        auto w = std::make_shared<Wallet>(walletDB);
        MyWalletObserver observer;
        ScopedSubscriber<wallet::IWalletObserver, wallet::Wallet> ws(&observer, w);
        auto nodeEndpoint = make_shared<proto::FlyClient::NetworkStd>(*w);
        nodeEndpoint->m_Cfg.m_PollPeriod_ms = 0;
        nodeEndpoint->m_Cfg.m_vNodes.push_back(io::Address::localhost().port(port));
        nodeEndpoint->Connect();
        w->SetNodeEndpoint(nodeEndpoint);
        io::Reactor::get_Current().run();
        size_t c = 0;

        walletDB->visitCoins([&c](const auto& coin)
        {
            ++c;
            return true;
        });

        WALLET_CHECK(c == count);
    };
    checkCoins(walletDB, 11, 32125); // +1 treasury
    checkCoins(walletDB2, 15, 32126);
}

void TestContract()
{
    auto walletDB = createWalletDB("wallet.db", true);
    auto binaryTreasury = createTreasury(walletDB, {});

    std::string contractPath = "test_contract.wasm";
    //std::string contractPath = "shader.wasm";//"test_contract.wasm";

    MyDebugger debugger(contractPath);

    TestLocalNode nodeA{ binaryTreasury, walletDB->get_MasterKdf() };
    auto w = std::make_shared<Wallet>(walletDB);
    MyWalletObserver observer;
    ScopedSubscriber<wallet::IWalletObserver, wallet::Wallet> ws(&observer, w);
    auto nodeEndpoint = make_shared<MyNetwork>(*w);
    nodeEndpoint->m_Cfg.m_PollPeriod_ms = 0;
    nodeEndpoint->m_Cfg.m_vNodes.push_back(io::Address::localhost().port(32125));
    nodeEndpoint->Connect();
    w->SetNodeEndpoint(nodeEndpoint);
    nodeA.SetDebugger(&debugger);
    //nodeA.GenerateBlocks(1);

    nodeA.GenerateBlocks(1, false);

    WALLET_CHECK(InvokeShader(w, walletDB, "test_app.wasm", contractPath, "role=manager,action=create"));

    nodeA.GenerateBlocks(1, false);
    nodeA.GenerateBlocks(1, false);
    nodeA.GenerateBlocks(1, false);
    nodeA.GenerateBlocks(1, false);

    auto tx = walletDB->getTxHistory(TxType::Contract);
    WALLET_CHECK(tx[0].m_status == TxStatus::Completed);
}

namespace
{
    // Event provides a basic wait and signal synchronization primitive.
    class Event
    {
    public:
        // Blocks until the event is fired.
        void Wait()
        {
            std::unique_lock lock(m_Mutex);
            m_cv.wait(lock, [&] { return m_Fired; });
        }

        // Sets signals the event, and unblocks any calls to wait().
        void Fire()
        {
            std::unique_lock lock(m_Mutex);
            m_Fired = true;
            m_cv.notify_all();
        }

        void Reset()
        {
            std::unique_lock lock(m_Mutex);
            m_Fired = false;
        }

    private:
        std::mutex m_Mutex;
        std::condition_variable m_cv;
        bool m_Fired = false;
    };

    class Debugger : public MyDebugger
    {
    public:
        enum class Event
        {
            BreakpointHit,
            Stepped,
            Paused,
            Output
        };

        struct Variable
        {
            std::string name;
            std::string type;
        };

        struct Call
        {
            std::string m_Name;
            int64_t m_Line = 1;
            int64_t m_Column = 1;
            std::string m_FilePath;
        };

        enum class Action
        {
            NoAction,
            Continue,
            Pause,
            StepIn,
            StepOut,
            SteppingOut,
            StepOver,
            SteppingOver
        };

        using EventHandler = std::function<void(Event, const std::string&)>;

        Debugger::Debugger(const EventHandler& onEvent, const std::string& contractPath)
            : MyDebugger(contractPath)
            , m_OnEvent(onEvent)
        {
            auto& c = m_CallStack.emplace_back();
            //c.m_FilePath = contractPath;
            c.m_Line = 0;
            c.m_Column = 0;
            c.m_Name = "CallFar";
        }

        //
        // Actions
        // 

        // Instructs the debugger to continue execution.
        void Run()
        {
            DoAction(Action::Continue);
        }

        // Instructs the debugger to Pause execution.
        void Pause()
        {
            DoAction(Action::Pause);
        }

        // Instructs the debugger to step forward one line.
        void StepForward()
        {
            DoAction(Action::StepOver);
        }

        void StepIn()
        {
            DoAction(Action::StepIn);
        }

        void StepOut()
        {
            DoAction(Action::StepOut);
        }

        //
        // Breakpoints
        //

        // Clears all set breakpoints for given source file.
        void ClearBreakpoints(const std::string& path)
        {
            std::unique_lock lock(m_Mutex);
            auto it = m_Breakpoints.find(path);
            if (it != m_Breakpoints.end())
            {
                it->second.clear();
            }
        }

        // Sets a new breakpoint on the given line.
        std::pair<size_t, bool> AddBreakpoint(const std::string& filePath, int64_t line)
        {
            std::unique_lock lock(m_Mutex);
            std::string filePathC = GetCanonicalPath(filePath);
            m_Breakpoints[filePathC].emplace(line);
            size_t id = std::hash<std::string>{}(filePathC);
            boost::hash_combine(id, line);
            return { id, CanSetBreakpoint(filePath, line) };
        }

        bool CanSetBreakpoint(const std::string& filePath, int64_t line)
        {
            // TODO: avoid linear search
            for (const auto& cu : m_DebugInfo->compilation_units())
            {
                const auto& lineTable = cu.get_line_table();
                auto it = std::find_if(lineTable.begin(), lineTable.end(), 
                    [&](const auto& entry)
                {
                    return GetCanonicalPath(entry.file->path) == filePath && entry.line == line && entry.is_stmt;
                });
                if (it != lineTable.end())
                {
                    return true;
                }
            }
            return false;
        }

        //
        // State
        //

        // GetCurrentLine() returns the currently executing line number.
        int64_t GetCurrentLine()
        {
            std::unique_lock lock(m_Mutex);
            assert(m_CallStack.empty());
            return m_CallStack.back().m_Line;
        }

        int64_t GetCurrentColumn()
        {
            std::unique_lock lock(m_Mutex);
            assert(m_CallStack.empty());
            return m_CallStack.back().m_Column;
        }

        std::vector<Debugger::Call> GetCallStack() const
        {
            std::unique_lock lock(m_Mutex);
            return m_CallStack;
        }

        std::vector<Variable> GetVariables()
        {
            std::unique_lock lock(m_Mutex);
            return m_Variables;
        }

        const std::string& GetFilePath() const
        {
            std::unique_lock lock(m_Mutex);
            assert(m_CallStack.empty());
            return m_CallStack.back().m_FilePath;
        }

        std::string GetTypeName(const dwarf::die& d)
        {
            try
            {
                if (d.has(dwarf::DW_AT::name))
                    return at_name(d);

                if (d.has(dwarf::DW_AT::type))
                    return GetTypeName(at_type(d));
            }
            catch (...)
            {

            }
            return {};
        }

        void DumpVariables(const dwarf::die& d)
        {
            switch (d.tag)
            {
            case dwarf::DW_TAG::variable:
            case dwarf::DW_TAG::formal_parameter:
            {
                auto& v = m_Variables.emplace_back();
                v.name = at_name(d);
                v.type = GetTypeName(at_type(d));
            }
            break;
            case dwarf::DW_TAG::subprogram:
            case dwarf::DW_TAG::inlined_subroutine:
                for (const auto& c : d)
                {
                    DumpVariables(c);
                }
                break;
            }
        }

        void DoDebug(const Wasm::Processor& proc)
        {
            std::vector<dwarf::die> stack;
            auto ip = proc.get_Ip();
            auto it = m_Compiler.m_IpMap.find(ip);
            if (it == m_Compiler.m_IpMap.end())
            {
                return;
            }
            auto myIp = it->second;

            std::unique_lock lock(m_Mutex);

            size_t prevStackHeight = m_CallStack.size();

            if (PrintPC(myIp, stack))
            {
                bool isStatement = (*m_CurrentLine)->is_stmt;
                auto r = die_pc_range(stack.front());
                if (r.begin()->low == myIp)
                {
                    if (m_PrevLine)
                    {
                        m_ReturnLines.push(m_PrevLine);
                    }
                    m_CallStack.emplace_back();
                }
                else if (!m_ReturnLines.empty())
                {
                    // TODO: this is a hack, i'm not sure if it is correct
                    const auto& t = *m_ReturnLines.top();
                    const auto& c = *m_CurrentLine;
                    if (t->file_index == c->file_index && t->line == c->line)
                    {
                        m_ReturnLines.pop();
                        m_CallStack.pop_back();
                    }
                }

                m_BvmIsReady = true;

                assert(!m_CallStack.empty());
                auto& call = m_CallStack.back();
                auto prevLine = call.m_Line;

                // Update current state
                call.m_Name = (*m_CurrentLine)->get_description();
                if (!stack.empty())
                {
                    auto& d = stack.back();
                    auto name = at_name(d);
                    call.m_Name = name + " " + call.m_Name;
                }

                call.m_FilePath = GetCanonicalPath((*m_CurrentLine)->file->path);
                call.m_Line = (*m_CurrentLine)->line;
                call.m_Column = (*m_CurrentLine)->column;

                // Update variables
                m_Variables.clear();
                for (auto& d : stack)
                {
                    DumpVariables(d);
                }
                if (call.m_Line == 0) // ignore
                {
                    return;
                }

                // Wait for the next user's action
                m_DapEvent.wait(lock, [this] {return m_NextAction != Action::NoAction; });

                // Process user's action
                if (m_NextAction == Action::Continue)
                {
                    if (isStatement && m_Breakpoints[call.m_FilePath].count(call.m_Line))
                    {
                        m_NextAction = Action::NoAction;
                        m_OnEvent(Event::BreakpointHit, "");
                        return;
                    }
                }
                else if (m_NextAction == Action::Pause)
                {
                    m_NextAction = Action::NoAction;
                    m_OnEvent(Event::Paused, "");
                    m_DapEvent.wait(lock, [this] {return m_NextAction != Action::NoAction; });
                }
                else if (m_NextAction == Action::StepOver)
                {
                    m_StackHeight = prevStackHeight;
                    m_NextAction = Action::SteppingOver;

                }
                else if (m_NextAction == Action::SteppingOver)
                {
                    if ((prevLine != call.m_Line && m_StackHeight == m_CallStack.size()) || m_StackHeight > m_CallStack.size())
                    {
                        m_NextAction = Action::NoAction;
                        m_OnEvent(Event::Stepped, "");
                        return;
                    }
                }
                else if (m_NextAction == Action::StepIn)
                {
                    m_NextAction = Action::NoAction;
                    m_OnEvent(Event::Stepped, "");
                    return;
                }
                else if (m_NextAction == Action::StepOut)
                {
                    if (prevStackHeight > 1)
                    {
                        m_StackHeight = prevStackHeight - 1;
                        m_NextAction = Action::SteppingOut;
                    }
                }
                else if (m_NextAction == Action::SteppingOut)
                {
                    if (m_StackHeight == m_CallStack.size())
                    {
                        m_NextAction = Action::NoAction;
                        m_OnEvent(Event::Stepped, "");
                        return;
                    }
                }
            }
        }

    private:

        void DoAction(Action action)
        {
            std::unique_lock lock(m_Mutex);
            m_NextAction = action;
            m_DapEvent.notify_one();
        }

    private:
        EventHandler m_OnEvent;
        mutable  std::mutex m_Mutex;

        Action m_NextAction = Action::Pause;
        bool m_BvmIsReady = false;
        bool m_WaitForDap = false;
        std::condition_variable m_BvmEvent;
        std::condition_variable m_DapEvent;

        std::vector<Variable> m_Variables;
        std::vector<Call> m_CallStack;
        std::stack<std::shared_ptr<dwarf::line_table::iterator>> m_ReturnLines;
        size_t m_StackHeight = 0;

        std::unordered_map<std::string, std::unordered_set<int64_t>> m_Breakpoints;
    };
}  // anonymous namespace

void TestDebugger(int argc, char* argv[])
{
    if (argc < 3)
        return;

    std::string contractPath = argv[1];
    std::string appPath = argv[2];

    std::string args;
    if (argc > 3)
    {
        args = argv[3];
    }
#ifdef OS_WINDOWS
    // Change stdin & stdout from text mode to binary mode.
    // This ensures sequences of \r\n are not changed to \n.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    ::MessageBoxA(NULL, "Waiting", "Waiting caption", MB_OK);
#endif  // OS_WINDOWS

    std::shared_ptr<dap::Writer> log;
#ifdef LOG_TO_FILE
    log = dap::file(LOG_TO_FILE);
#endif

    // Create the DAP session.
    // This is used to implement the DAP server.
    auto session = dap::Session::create();

    // Hard-coded identifiers for the one thread, frame, variable and source.
    // These numbers have no meaning, and just need to remain constant for the
    // duration of the service.
    const dap::integer threadId = 100;
    const dap::integer frameId = 200;
    const dap::integer variablesReferenceId = 300;
    const dap::integer sourceReferenceId = 400;

    // Signal events
    Event configured;
    Event terminate;

    // Event handlers from the Debugger.
    auto onDebuggerEvent = [&](Debugger::Event onEvent, const std::string& s)
    {
        switch (onEvent)
        {
            case Debugger::Event::Stepped:
            {
                // The debugger has single-line stepped. Inform the client.
                dap::StoppedEvent event;
                event.reason = "step";
                event.threadId = threadId;
                session->send(event);
                break;
            }
            case Debugger::Event::BreakpointHit:
            {
                // The debugger has hit a breakpoint. Inform the client.
                dap::StoppedEvent event;
                event.reason = "breakpoint";
                event.threadId = threadId;
                session->send(event);
                break;
            }
            case Debugger::Event::Paused:
            {
                // The debugger has been suspended. Inform the client.
                dap::StoppedEvent event;
                event.reason = "Pause";
                event.threadId = threadId;
                session->send(event);
                break;
            }
            case Debugger::Event::Output:
            {
                dap::OutputEvent event;
                event.output = s;
                session->send(event);
                break;
            }
        }
    };

    // Construct the debugger.
    Debugger debugger(onDebuggerEvent, contractPath);

    // Handle errors reported by the Session. These errors include protocol
    // parsing errors and receiving messages with no handler.
    session->onError([&](const char* msg)
    {
        if (log)
        {
            dap::writef(log, "dap::Session error: %s\n", msg);
            log->close();
        }
        terminate.Fire();
    });

    // The Initialize request is the first message sent from the client and
    // the response reports debugger capabilities.
    // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Initialize
    session->registerHandler([](const dap::InitializeRequest&)
    {
        dap::InitializeResponse response;
        response.supportsConfigurationDoneRequest = true;
        return response;
    });

    // When the Initialize response has been sent, we need to send the initialized
    // event.
    // We use the registerSentHandler() to ensure the event is sent *after* the
    // initialize response.
    // https://microsoft.github.io/debug-adapter-protocol/specification#Events_Initialized
    session->registerSentHandler(
        [&](const dap::ResponseOrError<dap::InitializeResponse>&)
    {
        session->send(dap::InitializedEvent());
    });

    // The Threads request queries the debugger's list of active threads.
    // This example debugger only exposes a single thread.
    // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Threads
    session->registerHandler([&](const dap::ThreadsRequest&)
    {
        dap::ThreadsResponse response;
        dap::Thread thread;
        thread.id = threadId;
        thread.name = "TheThread";
        response.threads.push_back(thread);
        return response;
    });

    // The StackTrace request reports the stack frames (call stack) for a given
    // thread. This example debugger only exposes a single stack frame for the
    // single thread.
    // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_StackTrace
    session->registerHandler(
        [&](const dap::StackTraceRequest& request)
        -> dap::ResponseOrError<dap::StackTraceResponse>
    {
        if (request.threadId != threadId) {
            return dap::Error("Unknown threadId '%d'", int(request.threadId));
        }

        auto callStack = debugger.GetCallStack();
        std::reverse(callStack.begin(), callStack.end());
        dap::StackTraceResponse response;
        for (const auto& entry : callStack)
        {
            dap::Source source;
            //source.sourceReference = sourceReferenceId;
            source.name = entry.m_Name;
            source.path = entry.m_FilePath;

            dap::StackFrame frame;
            frame.line = entry.m_Line;
            frame.column = entry.m_Column;
            frame.name = entry.m_Name;
            frame.id = frameId;
            frame.source = source;


            response.stackFrames.push_back(frame);
        }
        return response;
    });

    // The Scopes request reports all the scopes of the given stack frame.
    // This example debugger only exposes a single 'Locals' scope for the single
    // frame.
    // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Scopes
    session->registerHandler([&](const dap::ScopesRequest& request)
        -> dap::ResponseOrError<dap::ScopesResponse>
    {
        if (request.frameId != frameId)
        {
            return dap::Error("Unknown frameId '%d'", int(request.frameId));
        }

        dap::Scope scope;
        scope.name = "Locals";
        scope.presentationHint = "locals";
        scope.variablesReference = variablesReferenceId;

        dap::ScopesResponse response;
        response.scopes.push_back(scope);
        return response;
    });

    // The Variables request reports all the variables for the given scope.
    // This example debugger only exposes a single 'GetCurrentLine' variable for the
    // single 'Locals' scope.
    // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Variables
    session->registerHandler([&](const dap::VariablesRequest& request)
        -> dap::ResponseOrError<dap::VariablesResponse>
    {
        if (request.variablesReference != variablesReferenceId)
        {
            return dap::Error("Unknown variablesReference '%d'",
                int(request.variablesReference));
        }

        dap::VariablesResponse response;
        auto variables = debugger.GetVariables();
        for (const auto& v : variables)
        {
            auto& newVar = response.variables.emplace_back();
            newVar.name = v.name;
            newVar.type = v.type;
            newVar.value = "unknown";
        }

        return response;
    });

    // The Pause request instructs the debugger to Pause execution of one or all
    // threads.
    // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Pause
    session->registerHandler([&](const dap::PauseRequest&)
    {
        debugger.Pause();
        return dap::PauseResponse();
    });

    // The Continue request instructs the debugger to resume execution of one or
    // all threads.
    // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Continue
    session->registerHandler([&](const dap::ContinueRequest&)
    {
        debugger.Run();
        return dap::ContinueResponse();
    });

    // The Next request instructs the debugger to single line step for a specific
    // thread.
    // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Next
    session->registerHandler([&](const dap::NextRequest&)
    {
        debugger.StepForward();
        return dap::NextResponse();
    });

    // The StepIn request instructs the debugger to step-in for a specific thread.
    // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_StepIn
    session->registerHandler([&](const dap::StepInRequest&)
    {
        // Step-in treated as step-over as there's only one stack frame.
        debugger.StepIn();
        return dap::StepInResponse();
    });

    // The StepOut request instructs the debugger to step-out for a specific
    // thread.
    // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_StepOut
    session->registerHandler([&](const dap::StepOutRequest&)
    {
        // Step-out is not supported as there's only one stack frame.
        debugger.StepOut();
        return dap::StepOutResponse();
    });

    // The SetBreakpoints request instructs the debugger to clear and set a number
    // of line breakpoints for a specific source file.
    // This example debugger only exposes a single source file.
    // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_SetBreakpoints
    session->registerHandler([&](const dap::SetBreakpointsRequest& request)
    {
        dap::SetBreakpointsResponse response;
        auto breakpoints = request.breakpoints.value({});
        if (request.source.path)
        {
            auto path = GetCanonicalPath(*request.source.path);
            debugger.ClearBreakpoints(path);
            response.breakpoints.resize(breakpoints.size());
            for (size_t i = 0; i < breakpoints.size(); i++) {
                auto [id, verified] = debugger.AddBreakpoint(path, breakpoints[i].line);
                response.breakpoints[i].id = id;
                response.breakpoints[i].line = breakpoints[i].line;
                response.breakpoints[i].column = breakpoints[i].column;
                response.breakpoints[i].verified = verified;
            }
        }
        else
        {
            response.breakpoints.resize(breakpoints.size());
        }

        return response;
    });

    // The SetExceptionBreakpoints request configures the debugger's handling of
    // thrown exceptions.
    // This example debugger does not use any exceptions, so this is a no-op.
    // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_SetExceptionBreakpoints
    session->registerHandler([&](const dap::SetExceptionBreakpointsRequest&)
    {
        return dap::SetExceptionBreakpointsResponse();
    });

    // The Source request retrieves the source code for a given source file.
    // This example debugger only exposes one synthetic source file.
    // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Source
    session->registerHandler([&](const dap::SourceRequest& request)
        -> dap::ResponseOrError<dap::SourceResponse>
    {
        if (request.sourceReference != sourceReferenceId) {
            return dap::Error("Unknown source reference '%d'",
                int(request.sourceReference));
        }

        dap::SourceResponse response;
        response.content = "test";//sourceContent;
        return response;
    });

    // The Launch request is made when the client instructs the debugger adapter
    // to start the debuggee. This request contains the launch arguments.
    // This example debugger does nothing with this request.
    // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Launch
    session->registerHandler(
        [&](const dap::LaunchRequest& req)
    {
        req;
        return dap::LaunchResponse();
    });

    // Handler for disconnect requests
    session->registerHandler([&](const dap::DisconnectRequest& request)
    {
        if (request.terminateDebuggee.value(false))
        {
            terminate.Fire();
        }
        return dap::DisconnectResponse();
    });

    // The ConfigurationDone request is made by the client once all configuration
    // requests have been made.
    // This example debugger uses this request to 'start' the debugger.
    // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_ConfigurationDone
    session->registerHandler([&](const dap::ConfigurationDoneRequest&)
    {
        configured.Fire();
        return dap::ConfigurationDoneResponse();
    });

    // All the handlers we care about have now been registered.
    // We now bind the session to stdin and stdout to connect to the client.
    // After the call to bind() we should start receiving requests, starting with
    // the Initialize request.
    std::shared_ptr<dap::Reader> in = dap::file(stdin, false);
    std::shared_ptr<dap::Writer> out = dap::file(stdout, false);
    if (log)
    {
        session->bind(spy(in, log), spy(out, log));
    }
    else
    {
        session->bind(in, out);
    }

    // Wait for the ConfigurationDone request to be made.
    configured.Wait();
    //MessageBox(NULL, "Test", "TEst", MB_OK);
        // Broadcast the existance of the single thread to the client.
    dap::ThreadEvent threadStartedEvent;
    threadStartedEvent.reason = "started";
    threadStartedEvent.threadId = threadId;
    session->send(threadStartedEvent);

    auto walletDB = createWalletDB("c:\\Data\\Projects\\Beam\\beam-ee5.2RC\\wallet\\unittests\\wallet.db", true);
    auto binaryTreasury = createTreasury(walletDB, { 300'000'000'000UL, 300'000'000'000UL });


    TestLocalNode nodeA{ binaryTreasury, walletDB->get_MasterKdf() };
    auto w = std::make_shared<Wallet>(walletDB);
    MyWalletObserver observer;
    ScopedSubscriber<wallet::IWalletObserver, wallet::Wallet> ws(&observer, w);
    auto nodeEndpoint = make_shared<MyNetwork>(*w);
    nodeEndpoint->m_Cfg.m_PollPeriod_ms = 0;
    nodeEndpoint->m_Cfg.m_vNodes.push_back(io::Address::localhost().port(32125));
    nodeEndpoint->Connect();
    w->SetNodeEndpoint(nodeEndpoint);
    nodeA.SetDebugger(&debugger);
    //nodeA.GenerateBlocks(1);
    //debugger.Pause();
    nodeA.GenerateBlocks(1, false);
    // Wait for the ConfigurationDone request to be made.
    //configured.wait();



    WALLET_CHECK(InvokeShader(w, walletDB, appPath, contractPath, args));

    nodeA.GenerateBlocks(1, false);
    nodeA.GenerateBlocks(1, false);
    nodeA.GenerateBlocks(1, false);
    nodeA.GenerateBlocks(1, false);

    //auto tx = walletDB->getTxHistory(TxType::Contract);
    //WALLET_CHECK(tx[0].m_status == TxStatus::Completed);


    // Start the debugger in a paused state.
    // This sends a stopped event to the client.
    debugger.Pause();

    // Block until we receive a 'terminateDebuggee' request or encounter a session
    // error.
    terminate.Wait();

}

int main(int argc, char* argv[])
{
    const auto logLevel = LOG_LEVEL_DEBUG;
    const auto logger = beam::Logger::create(logLevel, logLevel);
    io::Reactor::Ptr reactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*reactor);

    Rules::get().FakePoW = true;
    Rules::get().Maturity.Coinbase = 0;
    Rules::get().pForks[1].m_Height = 1;
    Rules::get().pForks[2].m_Height = 1;
    Rules::get().pForks[3].m_Height = 1;
    Rules::get().UpdateChecksum();

    TestDebugger(argc, argv);

    //TestContract();
    //TestNode();


    return WALLET_CHECK_RESULT;
}
