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
#include "wallet_test_node.h"
#include "wallet/core/wallet_db.h"
#include <boost/filesystem.hpp>

namespace beam::wallet
{
    IWalletDB::Ptr createWalletDB(const std::string& path, bool generateSeed)
    {
        if (boost::filesystem::exists(path))
        {
            boost::filesystem::remove(path);
        }

        ECC::NoLeak<ECC::uintBig> seed;
        if (generateSeed)
        {
            void* p = reinterpret_cast<void*>(&seed.V);
            for (uint32_t i = 0; i < sizeof(seed.V); i++)
                ((uint8_t*)p)[i] = (uint8_t)rand();
        }
        else
        {
            seed.V = Zero;
        }

        auto walletDB = WalletDB::init(path, std::string("123"), seed, false);
        return walletDB;
    }

    ByteBuffer createTreasury(IWalletDB::Ptr db, const AmountList& amounts /*= { 5, 2, 1, 9 }*/)
    {
        Treasury treasury;
        PeerID pid;
        ECC::Scalar::Native sk;

        Treasury::get_ID(*db->get_MasterKdf(), pid, sk);

        Treasury::Parameters params;
        params.m_Bursts = 1U;
        //params.m_Maturity0 = 1;
        params.m_MaturityStep = 1;

        Treasury::Entry* plan = treasury.CreatePlan(pid, 0, params);
        beam::Height incubation = 0;
        for (size_t i = 0; i < amounts.size(); ++i)
        {
            if (i == 0)
            {
                plan->m_Request.m_vGroups.front().m_vCoins.front().m_Value = amounts[i];
                incubation = plan->m_Request.m_vGroups.front().m_vCoins.front().m_Incubation;
                continue;
            }

            auto& c = plan->m_Request.m_vGroups.back().m_vCoins.emplace_back();

            c.m_Incubation = incubation;
            c.m_Value = amounts[i];
        }

        plan->m_pResponse.reset(new Treasury::Response);
        uint64_t nIndex = 1;
        plan->m_pResponse->Create(plan->m_Request, *db->get_MasterKdf(), nIndex);

        for (const auto& group : plan->m_pResponse->m_vGroups)
        {
            for (const auto& treasuryCoin : group.m_vCoins)
            {
                CoinID cid;
                if (treasuryCoin.m_pOutput->Recover(0, *db->get_MasterKdf(), cid))
                {
                    Coin coin;
                    coin.m_ID = cid;
                    coin.m_maturity = treasuryCoin.m_pOutput->m_Incubation;
                    coin.m_confirmHeight = treasuryCoin.m_pOutput->m_Incubation;
                    db->saveCoin(coin);
                }
            }
        }

        Treasury::Data data;
        data.m_sCustomMsg = "LN";
        treasury.Build(data);

        Serializer ser;
        ser& data;

        ByteBuffer result;
        ser.swap_buf(result);

        return result;
    }

    void InitNodeToTest(Node& node
        , const ByteBuffer& binaryTreasury
        , Node::IObserver* observer
        , uint16_t port /*= 32125*/
        , uint32_t powSolveTime /*= 1000*/
        , const std::string& path /*= "mytest.db"*/
        , const std::vector<io::Address>& peers /*= {}*/
        , bool miningNode /*= true*/)
    {
        node.m_Cfg.m_Treasury = binaryTreasury;
        ECC::Hash::Processor() << Blob(node.m_Cfg.m_Treasury) >> Rules::get().TreasuryChecksum;

        boost::filesystem::remove(path);
        node.m_Cfg.m_sPathLocal = path;
        node.m_Cfg.m_Listen.port(port);
        node.m_Cfg.m_Listen.ip(INADDR_ANY);
        node.m_Cfg.m_MiningThreads = miningNode ? 1 : 0;
        node.m_Cfg.m_VerificationThreads = 1;
        node.m_Cfg.m_TestMode.m_FakePowSolveTime_ms = powSolveTime;
        node.m_Cfg.m_Connect = peers;

        node.m_Cfg.m_Dandelion.m_AggregationTime_ms = 0;
        node.m_Cfg.m_Dandelion.m_OutputsMin = 0;
        //Rules::get().Maturity.Coinbase = 1;
        Rules::get().FakePoW = true;

        ECC::uintBig seed = 345U;
        node.m_Keys.InitSingleKey(seed);

        node.m_Cfg.m_Observer = observer;
        Rules::get().UpdateChecksum();
        node.Initialize();
        node.m_PostStartSynced = true;
    }
}