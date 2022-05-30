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
#include "wallet/core/contracts/shaders_manager.h"
#include "core/fly_client.h"
#include "utilstrencodings.h"
#include <memory>
#include <inttypes.h>


#undef TRUE
#include "dap/io.h"
#include "dap/protocol.h"
#include "dap/session.h"

#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <unordered_set>
#include <boost/functional/hash.hpp>
#include <stack>
#include <optional>
#include <iomanip>
#include <algorithm>

#include "debugger/shader_debugger.h"

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
namespace fs = boost::filesystem;

namespace
{
    std::string FixPath(const std::string& p)
    {
        std::string res = fs::system_complete(p).make_preferred().string();
#ifdef OS_WINDOWS
        std::transform(begin(res), end(res), begin(res), [](unsigned char c) { return std::tolower(c); });
#endif // OS_WINDOWS

        return res;
    }

    std::string GetStackTraceName(const std::string& module, const ShaderDebugger::Frame& frame, const dap::optional<dap::StackFrameFormat>& format)
    {
        std::stringstream ss;

        bool showModules = (format && format->module && *format->module) || !format;
        bool showTypes = (format && format->parameterTypes && *format->parameterTypes) || !format;
        bool showNames = format && format->parameterNames && *format->parameterNames;
        bool showValues = format && format->parameterValues && *format->parameterValues;
        bool showParameters = (format && format->parameters && *format->parameters) || !format;
        bool showLines = (format && format->line && *format->line) || !format;

        if (showModules)
        {
            ss << module << '!';
        }

        if (frame.m_HasInfo)
        {
            ss << frame.m_Name;
            if (showParameters)
            {
                ss << '(';
                bool first = true;
                for (const auto& p : frame.m_Parameters)
                {
                    if (first)
                    {
                        first = false;
                    }
                    else
                    {
                        ss << ", ";
                    }
                    if (showTypes)
                    {
                        ss << p.type;
                    }
                    if (showNames)
                    {
                        if (showTypes)
                            ss << ' ';

                        ss << p.name;
                    }
                    if (showValues)
                    {
                        if (showNames)
                            ss << '=';

                        ss << p.value;
                    }
                }
                ss << ')';
            }

            if (showLines)
            {
                ss << " Line " << frame.m_Line;
            }
        }
        else
        {
            ss << std::hex << std::setw(8) << std::setfill('0') << frame.m_Address;
        }
        return ss.str();
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
        :public ManagerStdInWallet
    {
        bool m_Done = false;
        bool m_Err = false;
        bool m_Async = false;

        using ManagerStdInWallet::ManagerStdInWallet;

        void OnDone(const std::exception* pExc) override
        {
            m_Done = true;
            m_Err = !!pExc;

            if (pExc)
                std::clog << "Shader exec error: " << pExc->what() << std::endl;
            else
                std::clog << "Shader output: " << m_Out.str() << std::endl;

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
        MyManager man(walletDB, wallet);

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

        std::clog << "Executing shader..." << std::endl;

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
        MyDebugger(const std::string& shaderPath)
            : m_ShaderName(fs::path(shaderPath).filename().string())
        {
            // load symbols
            m_Buffer = MyManager::Load(shaderPath.c_str());
            ByteBuffer res;
            bvm2::Processor::Compile(m_Compiler, res, m_Buffer, bvm2::Processor::Kind::Contract);
            m_DebugInfo = std::make_unique<dwarf::dwarf>(std::make_shared<WasmLoader>(m_Compiler));
        }

        virtual void DoDebug(const Wasm::Processor& proc)
        {

        }

    protected:

        bool ContainsPC(const dwarf::die& d, dwarf::taddr pc) const
        {
            using namespace dwarf;
            return (d.has(DW_AT::low_pc) || d.has(DW_AT::ranges)) && die_pc_range(d).contains(pc);
        }

        bool FindFunctionByPC2(const dwarf::die& d, dwarf::taddr pc, std::vector<dwarf::die>& stack)
        {
            using namespace dwarf;

            // Scan children first to find most specific DIE
            bool found = false;
            for (auto& child : d)
            {
                found = FindFunctionByPC2(child, pc, stack);
                if (found)
                {
                    auto s = DumpDIE(child);
                    s;
                    break;
                }
            }
            switch (d.tag)
            {
            case DW_TAG::subprogram:
                try
                {
                    if (ContainsPC(d, pc))
                    {
                        found = true;
                        stack.push_back(d);
                    }
                }
                catch (const out_of_range&)
                {
                }
                catch (const value_type_mismatch&)
                {
                }
                break;
            case DW_TAG::inlined_subroutine:
                try
                {
                    if (ContainsPC(d, pc))
                    {
                        if ((found && stack.back().tag == DW_TAG::inlined_subroutine) || !found)
                        {
                            found = true;
                            stack.push_back(d);
                        }
                    }
                }
                catch (const out_of_range&)
                {
                }
                catch (const value_type_mismatch&)
                {
                }
                break;
            default:
                break;
            }
            return found;
        }

        bool FindFunctionByPC(const dwarf::die& d, dwarf::taddr pc, std::vector<dwarf::die>& stack)
        {
            using namespace dwarf;

            // Scan children first to find most specific DIE
            bool found = false;
            for (auto& child : d)
            {
                found = FindFunctionByPC(child, pc, stack);
                if (found)
                {
                    auto s = DumpDIE(child);
                    s;
                    break;
                }
            }
            switch (d.tag)
            {
            case DW_TAG::subprogram:
            case DW_TAG::inlined_subroutine:
                try
                {
                    if (found || ContainsPC(d, pc))
                    {
                        found = true;
                        stack.push_back(d);
                    }
                }
                catch (const out_of_range&)
                {
                }
                catch (const value_type_mismatch&)
                {
                }
                break;
            default:
                break;
            }
            return found;
        }

        std::string DumpDIE(const dwarf::die& node, int intent = 0)
        {
            std::stringstream ss;
            for (int i = 0; i < intent; ++i)
                ss << "      ";
            ss << "<" << node.get_section_offset() << "> " 
               << to_string(node.tag) << '\n';
            
            for (auto& attr : node.attributes())
            {
                for (int i = 0; i < intent; ++i)
                    ss << "      ";

                ss << "      " << to_string(attr.first) << " " 
                   << to_string(attr.second) << '\n';
            }


            for (const auto& child : node)
                ss << DumpDIE(child, intent + 1);
            return ss.str();
        }

        struct FunctionInfo
        {
            std::vector<dwarf::die> m_Dies;
            dwarf::line_table::iterator m_Line;
        };

        std::optional<FunctionInfo> FindFunctionByPC(dwarf::taddr pc)
        {
            for (auto& cu : m_DebugInfo->compilation_units())
            {
                if (ContainsPC(cu.root(), pc))
                {
                    // Map PC to a line
                    auto& lt = cu.get_line_table();
                    auto it = lt.find_address(pc);
                    if (it == lt.end())
                    {
                        return {};
                    }

                    vector<dwarf::die> stack;
                    if (FindFunctionByPC2(cu.root(), pc, stack))
                    {
                        return FunctionInfo{ std::move(stack), it };
                    }
                    break;
                }
            }
            return {};
        }

        bool LookupSourceLine(dwarf::taddr pc, std::vector<dwarf::die>& stack)
        {
            for (auto& cu : m_DebugInfo->compilation_units())
            {
                if (ContainsPC(cu.root(), pc))
                {
                    // Map PC to a line
                    auto& lt = cu.get_line_table();
                    auto it = lt.find_address(pc);
                    if (it == lt.end())
                        return false;

                    if (m_CurrentLine)
                    {
                        if (*m_CurrentLine == it)
                            return false;

                        m_PrevLine = m_CurrentLine;
                    }
                    m_CurrentLine = std::make_shared<dwarf::line_table::iterator>(it);

                    // Map PC to an object
                    // XXX Index/helper/something for looking up PCs
                    // XXX DW_AT_specification and DW_AT_abstract_origin
                    if (FindFunctionByPC(cu.root(), pc, stack))
                    {
                        for (auto& d : stack)
                        {
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
        std::string m_ShaderName;
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
            , const std::string& path = "mytest.db"
            , uint16_t port = 32125
            , const std::vector<io::Address>& peers = {}
        )
        {
            m_Node.m_Cfg.m_Treasury = binaryTreasury;
            ECC::Hash::Processor() << Blob(m_Node.m_Cfg.m_Treasury) >> Rules::get().TreasuryChecksum;

            fs::remove(path);
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

        void SetDebugger(ShaderDebugger* d)
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
        ShaderDebugger* m_Debugger = nullptr;
    };
}

void TestNode()
{
    auto walletDB = createWalletDB("wallet.db", true);
    auto binaryTreasury = createTreasury(walletDB, {});
    auto walletDB2 = createWalletDB("wallet2.db", true);

    TestLocalNode nodeA{ binaryTreasury, walletDB->get_MasterKdf() };
    TestLocalNode nodeB{ binaryTreasury, walletDB2->get_MasterKdf(), "mytest2.db", 32126, {io::Address::localhost().port(32125)} };

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

    ShaderDebugger debugger(contractPath, [] (auto, const auto&) {});

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
            std::string value;
        };

        struct Frame
        {
            bool m_HasInfo = false;
            std::string m_Name;
            int64_t m_Line = 1;
            int64_t m_Column = 1;
            std::string m_FilePath;
            dwarf::taddr m_FrameBase;
            dwarf::taddr m_Address;
            int64_t m_ID;
            dwarf::die m_Die;
            std::vector<Variable> m_Parameters;
        };

        enum class Action
        {
            NoAction,
            Continue,
            Pause,
            StepIn,
            SteppingIn,
            StepOut,
            SteppingOut,
            StepOver,
            SteppingOver
        };

        using EventHandler = std::function<void(Event, const std::string&)>;

        Debugger(const EventHandler& onEvent, const std::string& contractPath)
            : MyDebugger(contractPath)
            , m_OnEvent(onEvent)
        {
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
        std::pair<int32_t, bool> AddBreakpoint(const std::string& filePath, int64_t line)
        {
            std::unique_lock lock(m_Mutex);
            std::string filePathC = FixPath(filePath);
            m_Breakpoints[filePathC].emplace(line);
            size_t id = std::hash<std::string>{}(filePathC);
            boost::hash_combine(id, line);
            return { static_cast<int32_t>(id), CanSetBreakpoint(filePathC, line) };
        }

        bool CanSetBreakpoint(const std::string& filePath, int64_t line)
        {
            fs::path path(filePath);
            // TODO: avoid linear search
            for (const auto& cu : m_DebugInfo->compilation_units())
            {
                const auto& lineTable = cu.get_line_table();
                static_assert(std::is_same_v<std::iterator_traits<dwarf::line_table::iterator>::iterator_category, std::forward_iterator_tag> == true);
                auto it = std::find_if(lineTable.begin(), lineTable.end(),
                    [&](const auto& entry)
                    {
                        return entry.line == line && entry.is_stmt && fs::equivalent(path, fs::path(entry.file->path));
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

        std::vector<Debugger::Frame> GetCallStack() const
        {
            std::unique_lock lock(m_Mutex);
            return m_CallStack;
        }

        std::string GetStackTraceName(const Frame& frame, const dap::optional<dap::StackFrameFormat>& format)
        {
            std::stringstream ss;

            bool showModules = (format && format->module && *format->module) || !format;
            bool showTypes = (format && format->parameterTypes && *format->parameterTypes) || !format;
            bool showNames = format&& format->parameterNames&&* format->parameterNames;
            bool showValues = format&& format->parameterValues&&* format->parameterValues;
            bool showParameters = (format && format->parameters && *format->parameters) || !format;
            bool showLines = (format && format->line && *format->line) || !format;

            if (showModules)
            {
                ss << m_ShaderName << '!';
            }

            if (frame.m_HasInfo)
            {
                ss << frame.m_Name;
                if (showParameters)
                {
                    ss << '(';
                    bool first = true;
                    for (const auto& p : frame.m_Parameters)
                    {
                        if (first)
                        {
                            first = false;
                        }
                        else
                        {
                            ss << ", ";
                        }
                        if (showTypes)
                        {
                            ss << p.type;
                        }
                        if (showNames)
                        {
                            if (showTypes)
                                ss << ' ';

                            ss << p.name;
                        }
                        if (showValues)
                        {
                            if (showNames)
                                ss << '=';

                            ss << p.value;
                        }
                    }
                    ss << ')';
                }

                if (showLines)
                {
                    ss << " Line " << frame.m_Line;
                }
            }
            else
            {
                ss << std::hex << std::setw(8) << std::setfill('0') << frame.m_Address;
            }
            return ss.str();
        }

        const Frame* FindFrameByID(int64_t frameID) const
        {
            for (const auto& frame : m_CallStack)
            {
                if (frameID == frame.m_ID)
                {
                    return &frame;
                }
            }
            return nullptr;
        }

        int64_t GetVariableReferenceID(int64_t frameID) const
        {
            std::unique_lock lock(m_Mutex);
            auto f = FindFrameByID(frameID);
            if (f)
            {
                return f->m_ID;
            }
            return -1;
        }

        std::optional<std::vector<Variable>> GetVariables(int64_t id, bool hex) const
        {
            std::unique_lock lock(m_Mutex);
            auto f = FindFrameByID(id);
            if (!f)
            {
                return {};
            }
            return LoadVariables(*f, hex);
        }

        const std::string& GetFilePath() const
        {
            std::unique_lock lock(m_Mutex);
            assert(m_CallStack.empty());
            return m_CallStack.back().m_FilePath;
        }

        std::string GetTypeName(const dwarf::die& d) const
        {
            using namespace dwarf;
            try
            {
                switch (d.tag)
                {
                case DW_TAG::const_type:
                    return "const " + GetTypeName(at_type(d));
                case DW_TAG::pointer_type:
                    return GetTypeName(at_type(d)) + "*";
                case DW_TAG::reference_type:
                    return GetTypeName(at_type(d)) + "&";
                case DW_TAG::rvalue_reference_type:
                    return GetTypeName(at_type(d)) + "&&";
                default:
                    return at_name(d);
                }
            }
            catch (...)
            {

            }
            return {};
        }

        struct MyContext : dwarf::expr_context
        {
            dwarf::taddr m_fb;
            MyContext(dwarf::taddr fb) 
                : m_fb(fb)
            {}
            dwarf::taddr fbreg() override
            {
                return m_fb;
            }
            dwarf::taddr reg(unsigned regnum) override
            {
                return 0;
            }
        };

        std::vector<Variable> LoadVariables(const Frame& frame, bool hex) const
        {
            std::vector<Variable> res;
            using namespace dwarf;
            switch (frame.m_Die.tag)
            {
            case DW_TAG::subprogram:
            case DW_TAG::inlined_subroutine:
                for (const auto& c : frame.m_Die)
                {
                    if (c.tag == DW_TAG::formal_parameter ||
                        c.tag == DW_TAG::variable)
                    {
                        auto& v = res.emplace_back();
                        try
                        {
                            if (c.has(DW_AT::name))
                            {
                                v.name = at_name(c);
                            }
                            auto type = at_type(c);
                            v.type = GetTypeName(type);
                            v.value = "unknown";
                            if (c.has(DW_AT::location))
                            {
                                auto loc = c[DW_AT::location];
                                if (loc.get_type() == value::type::exprloc)
                                {
                                    auto expr = loc.as_exprloc();
                                    MyContext ctx(frame.m_FrameBase);
                                    auto val = expr.evaluate(&ctx, 0);
                                    if (type.has(DW_AT::byte_size) && type.has(DW_AT::encoding))
                                    {
                                        auto size = at_byte_size(type, &ctx);
                                        size;
                                    }

                                    std::stringstream ss;
                                    if (hex)
                                    {
                                        ss << "0x" << std::hex << std::setw(8) << std::setfill('0');
                                    }
                                    ss << Wasm::from_wasm(*reinterpret_cast<int32_t*>(val.value));
                                    v.value = ss.str();
                                }
                                
                            }
                        }
                        catch (...) {}
                    }
                }
                break;
            default:
                break;
            }
            return res;
        }

        std::string to_string(const std::vector<Variable>& vars)
        {
            std::stringstream ss;
            bool first = true;
            for (const auto& v : vars)
            {
                if (first)
                {
                    first = false;
                }
                else
                {
                    ss << ", ";
                }
                ss << v.type;
            }
            return ss.str();
        }

        std::vector<Variable> GetFormalParameters(const dwarf::die& d)
        {
            std::vector<Variable> res;
            using namespace dwarf;
            switch (d.tag)
            {
            case DW_TAG::subprogram:
            case DW_TAG::inlined_subroutine:
                for (const auto& c : d)
                {
                    if (c.tag == DW_TAG::formal_parameter)
                    {
                        if (c.has(DW_AT::artificial) && at_artificial(c) == true)
                        {
                            continue;
                        }
                        auto& v = res.emplace_back();
                        try
                        {
                            if (c.has(DW_AT::name))
                            {
                                v.name = at_name(c);
                            }
                            v.type = GetTypeName(at_type(c));
                        }
                        catch (...) {}
                    }
                }
                break;
            default:
                break;
            }
            return res;
        }

        void DoDebug(const Wasm::Processor& proc)
        {
            using namespace dwarf;
            std::vector<die> stack;
            auto ip = proc.get_Ip();
            auto it = m_Compiler.m_IpMap.find(ip);
            if (it == m_Compiler.m_IpMap.end())
            {
                return;
            }
            auto myIp = it->second;

            std::unique_lock lock(m_Mutex);

            if (LookupSourceLine(myIp, stack))
            {
                // Update current state
                auto stackHeight = m_CallStack.size();
                m_CallStack.clear();
                std::vector<taddr> callStack;
                callStack.reserve(proc.m_CallStack.size());
                for (auto p : proc.m_CallStack)
                {
                    callStack.push_back(proc.m_Stack.m_pPtr[p]);
                }

                callStack.push_back(ip);
                for (auto addr : callStack)
                {
                    auto it2 = m_Compiler.m_IpMap.find(addr);
                    if (it2 != m_Compiler.m_IpMap.end())
                    {
                        if (auto funcInfo = FindFunctionByPC(it2->second); funcInfo)
                        {
                            const auto& c = funcInfo->m_Line;
                            uint64_t line = c->line;
                            uint64_t column = c->column;
                            std::string filePath = c->file->path;
                            m_CallStack.resize(m_CallStack.size() + funcInfo->m_Dies.size());
                            size_t i = m_CallStack.size() - 1;
                            for (const auto& die : funcInfo->m_Dies)
                            {
                                auto& frame = m_CallStack[i--];
                                auto addressPos = (i < proc.m_CallStack.size()) ? proc.m_CallStack[i] : 0;
                                if (proc.m_Stack.m_Pos <= addressPos)
                                {
                                    return;
                                }
                                auto framePos = proc.m_Stack.m_pPtr[addressPos + 1];
                                if ((framePos & Wasm::MemoryType::Mask) != Wasm::MemoryType::Stack)
                                {
                                    return;
                                }
                                framePos &= ~Wasm::MemoryType::Mask;
                                auto frameBase = reinterpret_cast<uint8_t*>(proc.m_Stack.m_pPtr) + framePos;
                                frame.m_Address = addr;
                                frame.m_Die = die;
                                auto d = die;
                                if (d.tag == DW_TAG::subprogram &&
                                    d.has(DW_AT::specification))
                                {
                                    d = at_specification(d);
                                }
                                else if (d.tag == DW_TAG::inlined_subroutine)
                                {
                                    d = at_abstract_origin(d);
                                }

                                if (d.has(DW_AT::name))
                                {
                                    frame.m_HasInfo = true;
                                    frame.m_Name = at_name(d);
                                    frame.m_FilePath = FixPath(filePath);
                                    frame.m_Line = line;
                                    frame.m_Column = column;
                                    frame.m_ID = it2->second;
                                    frame.m_Parameters = GetFormalParameters(d);
                                    frame.m_FrameBase = reinterpret_cast<taddr>(frameBase);
                                }
                                else
                                {
                                    frame.m_Name = "[External Code]";
                                }
                                if (die.tag == DW_TAG::inlined_subroutine)
                                {
                                    if (die.has(DW_AT::call_file))
                                    {
                                        auto fileIndex = (unsigned int)die[DW_AT::call_file].as_uconstant();
                                        const auto& lineTable = dynamic_cast<const compilation_unit&>(die.get_unit()).get_line_table();
                                        filePath = lineTable.get_file(fileIndex)->path;
                                    }
                                    if (die.has(DW_AT::call_line))
                                        line = die[DW_AT::call_line].as_uconstant();
                                    if (die.has(DW_AT::call_column))
                                        column = die[DW_AT::call_column].as_uconstant();
                                }
                            }
                            continue;
                        }
                    } 
                    std::stringstream ss;
                    ss << std::hex << std::setw(8) << std::setfill('0') << addr << "()";
                    auto& frame = m_CallStack.emplace_back();
                    frame.m_Name = ss.str();
                    frame.m_Address = addr;
                }
                m_Processor = &proc;
                // Process user's action
                ProcessAction(stackHeight);
                // Wait for the next user's action
                m_DapEvent.wait(lock, [this] {return m_NextAction != Action::NoAction; });
                // Continue execution
            }
        }

        std::vector<uint8_t> ReadMemory(Wasm::Word offset, Wasm::Word count)
        {
            std::vector<uint8_t> data;

            return data;
        }

        void ProcessAction(size_t stackHeight)
        {
            auto& c = *m_CurrentLine;
            switch (m_NextAction)
            {
            case Action::Continue:

                if (const auto& call = m_CallStack.back();
                    c->is_stmt && m_Breakpoints[call.m_FilePath].count(call.m_Line))
                {
                    EmitEvent(Event::BreakpointHit);
                }
                break;
            case Action::Pause:
                if (c->is_stmt)
                {
                    EmitEvent(Event::Paused);
                }
                break;
            case Action::StepOver:

                m_StackHeight = stackHeight;
                m_NextAction = Action::SteppingOver;
                break;
            case Action::SteppingOver:

                if (c->is_stmt && m_CallStack.size() <= m_StackHeight)
                {
                    EmitEvent(Event::Stepped);
                }
                break;
            case Action::StepIn:

                m_NextAction = Action::SteppingIn;
                break;
            case Action::SteppingIn:

                EmitEvent(Event::Stepped);
                break;
            case Action::StepOut:

                if (!m_CallStack.empty())
                {
                    m_StackHeight = stackHeight - 1;
                    m_NextAction = Action::SteppingOut;
                }
                break;
            case Action::SteppingOut:

                if (m_StackHeight == m_CallStack.size())
                {
                    EmitEvent(Event::Stepped);
                }
                break;
            case Action::NoAction:
                break;
            }
        }

        void EmitEvent(Debugger::Event event = Event::Stepped)
        {
            m_NextAction = Action::NoAction;
            m_OnEvent(event, "");
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

        const Wasm::Processor* m_Processor = nullptr;

        Action m_NextAction = Action::Pause;
        std::condition_variable m_BvmEvent;
        std::condition_variable m_DapEvent;

        std::vector<Frame> m_CallStack;
        size_t m_StackHeight = 0;

        std::unordered_map<std::string, std::unordered_set<int64_t>> m_Breakpoints;
    };
}  // anonymous namespace

void TestDebugger(int argc, char* argv[])
{
    if (argc < 3)
        return;

    std::string rootPath = argv[1];
    std::string contractPath = rootPath + argv[2];
    std::string appPath = rootPath + argv[3];

    std::string args;
    if (argc > 4)
    {
        args = argv[4];
    }
#ifdef OS_WINDOWS
    // Change stdin & stdout from text mode to binary mode.
    // This ensures sequences of \r\n are not changed to \n.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    auto len = GetCurrentDirectoryA(0, nullptr);
    std::vector<char> buf;
    buf.resize(len);
    GetCurrentDirectoryA(len, buf.data());
  //  LPCSTR str = fs::system_complete(fs::current_path()).string().c_str();
    ::MessageBoxA(NULL, buf.data(), "Waiting", MB_OK);
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
    const dap::integer sourceReferenceId = 400;

    // Signal events
    Event configured;
    Event terminate;

    // Event handlers from the Debugger.
    auto onDebuggerEvent = [&](ShaderDebugger::Event onEvent, const std::string& s)
    {
        switch (onEvent)
        {
        case ShaderDebugger::Event::Stepped:
        {
            // The debugger has single-line stepped. Inform the client.
            dap::StoppedEvent event;
            event.reason = "step";
            event.threadId = threadId;
            session->send(event);
            break;
        }
        case ShaderDebugger::Event::BreakpointHit:
        {
            // The debugger has hit a breakpoint. Inform the client.
            dap::StoppedEvent event;
            event.reason = "breakpoint";
            event.threadId = threadId;
            session->send(event);
            break;
        }
        case ShaderDebugger::Event::Paused:
        {
            // The debugger has been suspended. Inform the client.
            dap::StoppedEvent event;
            event.reason = "pause";
            event.threadId = threadId;
            session->send(event);
            break;
        }
        case ShaderDebugger::Event::Output:
        {
            dap::OutputEvent event;
            event.output = s;
            session->send(event);
            break;
        }
        }
    };

    // Construct the debugger.
    ShaderDebugger debugger(contractPath, onDebuggerEvent);

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
    session->registerHandler([](const dap::InitializeRequest& request)
        {
            dap::InitializeResponse response;
            response.supportsConfigurationDoneRequest = true;
            response.supportsValueFormattingOptions = true;
            //response.supportsReadMemoryRequest = true;
            response.supportsEvaluateForHovers = false;
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
            if (request.threadId != threadId)
            {
                return dap::Error("Unknown threadId '%d'", int(request.threadId));
            }
            
            auto callStack = debugger.GetCallStack();
            std::reverse(callStack.begin(), callStack.end());

            size_t startFrame = request.startFrame ? size_t(*request.startFrame) : 0;
            size_t endFrame = request.levels ? std::min(callStack.size(), static_cast<size_t>(*request.levels) + startFrame) : callStack.size();

            dap::StackTraceResponse response;
            for (; startFrame < endFrame; ++startFrame)
            {
                const auto& entry = callStack[startFrame];
                dap::Source source;
                if (entry.m_FilePath.empty())
                    source.name = entry.m_Name;
                source.path = entry.m_FilePath;

                auto& frame = response.stackFrames.emplace_back();
                frame.line = entry.m_Line;
                frame.column = entry.m_Column;

                frame.name = GetStackTraceName("module", entry, request.format);
                frame.id = entry.m_ID;
                frame.source = source;
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
            auto id = debugger.GetVariableReferenceID(request.frameId);
            if (request.frameId == -1)
            {
                return dap::Error("Unknown frameId '%d'", int(request.frameId));
            }

            dap::ScopesResponse response;

            dap::Scope scope;
            scope.name = "Locals";
            scope.presentationHint = "locals";
            scope.variablesReference = id;
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
            bool hex = request.format && request.format->hex;
            auto variables = debugger.GetVariables(request.variablesReference, hex);
            if (!variables)
            {
                return dap::Error("Unknown variablesReference '%d'",
                    int(request.variablesReference));
            }

            dap::VariablesResponse response;
            for (const auto& v : *variables)
            {
                auto& newVar = response.variables.emplace_back();
                newVar.name = v.name;
                newVar.type = v.type;
                newVar.value = v.value;
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
                auto path = FixPath(*request.source.path);
                debugger.ClearBreakpoints(path);
                response.breakpoints.resize(breakpoints.size());
                for (size_t i = 0; i < breakpoints.size(); i++)
                {
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
            if (request.sourceReference != sourceReferenceId)
            {
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

    // Reads bytes from memory at the provided location.
    // Clients should only call this request if the capability ‘supportsReadMemoryRequest’ is true.
    // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_ReadMemory
    session->registerHandler([&](const dap::ReadMemoryRequest& request)
        {
            dap::ReadMemoryResponse response;
            //auto b = debugger.ReadMemory(Wasm::Word(*request.offset), Wasm::Word(request.count));
            //response.data = EncodeBase64(b.data(), b.size());
            ////request.offset
            ////response.
            return response;
        });

    //// Reads bytes from memory at the provided location.
    //// Clients should only call this request if the capability ‘supportsReadMemoryRequest’ is true.
    //// https://microsoft.github.io/debug-adapter-protocol/specification#Requests_ReadMemory
    session->registerHandler([&](const dap::EvaluateRequest& request)
        {
            dap::EvaluateResponse response;
            response.result = "tesy resul evauatse";
            response.memoryReference = "0x000";
            return response;
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

    auto walletDB = createWalletDB(rootPath + "wallet.db", true);
    auto binaryTreasury = createTreasury(walletDB, { 300'000'000'000UL, 300'000'000'000UL });


    TestLocalNode nodeA{ binaryTreasury, walletDB->get_MasterKdf(), rootPath + "node.db" };
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

void PrintInfo(const char* path, int line)
{
    struct InfoDebuger : MyDebugger 
    {
        using MyDebugger::MyDebugger;

        void PrintLine(int line)
        {
            auto it = m_Compiler.m_IpMap.find(line);
            if (it == m_Compiler.m_IpMap.end())
            {
                return;
            }
            auto myIp = it->second;
            std::vector<dwarf::die> stack;
            if (LookupSourceLine(myIp, stack))
            {
                auto funcInfo = FindFunctionByPC(myIp);
                const auto& c = funcInfo->m_Line;
                std::cout << "Line:\t" << c->line
                          << "\nColumn:\t"  << c->column 
                          << "\nPath:\t" << c->file->path << std::endl;

                std::cout << DumpDIE(stack[0]) << std::endl;
                
                

            }
        }
    };    
    InfoDebuger debugger(path);
    debugger.PrintLine(line);
}

int main(int argc, char* argv[])
{
    const auto logLevel = LOG_LEVEL_ERROR;
    const auto logger = beam::Logger::create(logLevel, logLevel);
    io::Reactor::Ptr reactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*reactor);

    Rules::get().FakePoW = true;
    Rules::get().Maturity.Coinbase = 0;
    Rules::get().pForks[1].m_Height = 1;
    Rules::get().pForks[2].m_Height = 1;
    Rules::get().pForks[3].m_Height = 1;
    Rules::get().UpdateChecksum();

    if (strcmp(argv[1], "print-info") == 0)
    {
        int ip = strtol(argv[3], nullptr, 10);
        PrintInfo(argv[2], ip);
        return 0;
    }
    TestDebugger(argc, argv);

    //TestContract();
    //TestNode();


    return WALLET_CHECK_RESULT;
}
