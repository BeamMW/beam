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


WALLET_TEST_INIT

#include "wallet_test_environment.cpp"

using namespace beam;
using namespace beam::wallet;

namespace
{
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

    struct MyObserver : wallet::IWalletObserver
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

    class TestLocalNode : private Node::IObserver
    {
    public:
        TestLocalNode(const ByteBuffer& binaryTreasury
            , Key::IKdf::Ptr pKdf
            , uint16_t port = 32125
            , const std::string& path = "mytest.db"
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
        MyObserver observer;
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
    case DW_TAG::subprogram:
    case DW_TAG::inlined_subroutine:
        try {
            if (found || die_pc_range(d).contains(pc)) {
                found = true;
                stack->push_back(d);
            }
        }
        catch (out_of_range& ) {
        }
        catch (value_type_mismatch& ) {
        }
        break;
    default:
        break;
    }
    return found;
}

void DumpDIE(const dwarf::die& node)
{
    printf("<%" PRIx64 "> %s\n",
        node.get_section_offset(),
        to_string(node.tag).c_str());
    for (auto& attr : node.attributes())
        printf("      %s %s\n",
            to_string(attr.first).c_str(),
            to_string(attr.second).c_str());
}

void PrintPC(const dwarf::dwarf& dw, dwarf::taddr pc)
{
    for (auto& cu : dw.compilation_units())
    {
        if (die_pc_range(cu.root()).contains(pc)) {
            // Map PC to a line
            auto& lt = cu.get_line_table();
            auto it = lt.find_address(pc);
            if (it == lt.end())
                printf("UNKNOWN\n");
            else
                printf("%s\n",
                    it->get_description().c_str());

            // Map PC to an object
            // XXX Index/helper/something for looking up PCs
            // XXX DW_AT_specification and DW_AT_abstract_origin
            vector<dwarf::die> stack;
            if (FindPC(cu.root(), pc, &stack)) {
                bool first = true;
                for (auto& d : stack) {
                    if (!first)
                        printf("\nInlined in:\n");
                    first = false;
                    DumpDIE(d);
                }
            }
            break;
        }
    }
}

void TestContract()
{
    auto walletDB = createWalletDB("wallet.db", true);
    auto binaryTreasury = createTreasury(walletDB, {});

    TestLocalNode nodeA{ binaryTreasury, walletDB->get_MasterKdf() };
    auto w = std::make_shared<Wallet>(walletDB);
    MyObserver observer;
    ScopedSubscriber<wallet::IWalletObserver, wallet::Wallet> ws(&observer, w);
    auto nodeEndpoint = make_shared<MyNetwork>(*w);
    nodeEndpoint->m_Cfg.m_PollPeriod_ms = 0;
    nodeEndpoint->m_Cfg.m_vNodes.push_back(io::Address::localhost().port(32125));
    nodeEndpoint->Connect();
    w->SetNodeEndpoint(nodeEndpoint);
    
    //nodeA.GenerateBlocks(1);

    std::string contractPath = "test_contract.wasm";
    // load symbols
    auto buffer = MyManager::Load(contractPath.c_str());
    Wasm::Reader inp;
    inp.m_p0 = &*buffer.begin();
    inp.m_p1 = inp.m_p0+buffer.size();
    Wasm::Compiler c;
    c.Parse(inp);
    dwarf::dwarf dw(std::make_shared<WasmLoader>(c));
    PrintPC(dw, 45);
   

    WALLET_CHECK(InvokeShader(w, walletDB, "test_app.wasm", contractPath, "role=manager,action=create"));

    nodeA.GenerateBlocks(1, false);
    nodeA.GenerateBlocks(1, false);
    nodeA.GenerateBlocks(1, false);
    nodeA.GenerateBlocks(1, false);

    auto tx = walletDB->getTxHistory(TxType::Contract);
    WALLET_CHECK(tx[0].m_status == TxStatus::Completed);
}

int main()
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

    TestContract();
    TestNode();

    
    return WALLET_CHECK_RESULT;
}
