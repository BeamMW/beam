// Copyright 2020 The Beam Team
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

#include "wallet/core/common.h"
#include "wallet/core/wallet_network.h"
#include "wallet/core/wallet.h"
#include "wallet/transactions/lelantus/push_transaction.h"
#include "wallet/transactions/lelantus/pull_transaction.h"
#include "keykeeper/local_private_key_keeper.h"
#include "wallet/core/simple_transaction.h"
#include "core/unittest/mini_blockchain.h"
#include "utility/test_helpers.h"

#include "node/node.h"

#include "test_helpers.h"

#include <boost/filesystem.hpp>

using namespace beam;
using namespace std;
using namespace ECC;

WALLET_TEST_INIT

#include "wallet_test_environment.cpp"

namespace
{
    const AmountList kDefaultTestAmounts = { 5000, 2000, 1000, 9000 };

    void InitOwnNodeToTest(Node& node, const ByteBuffer& binaryTreasury, Node::IObserver* observer, Key::IPKdf::Ptr ownerKey, uint16_t port = 32125, uint32_t powSolveTime = 1000, const std::string& path = "mytest.db", const std::vector<io::Address>& peers = {}, bool miningNode = true)
    {
        node.m_Keys.m_pOwner = ownerKey;
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

        node.m_Cfg.m_Observer = observer;
        Rules::get().UpdateChecksum();
        node.Initialize();
        node.m_PostStartSynced = true;
    }
}

void TestSimpleTx()
{
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    int completedCount = 4;
    auto completeAction = [&mainReactor, &completedCount](auto)
    {
        --completedCount;
        if (completedCount == 0)
        {
            mainReactor->stop();
        }
    };

    auto senderWalletDB = createSenderWalletDB(0, 0);
    auto binaryTreasury = createTreasury(senderWalletDB, kDefaultTestAmounts);
    TestWalletRig sender("sender", senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);

    auto pushTxCreator = std::make_shared<lelantus::PushTransaction::Creator>();
    sender.m_Wallet.RegisterTransactionType(TxType::PushTransaction, std::static_pointer_cast<BaseTransaction::Creator>(pushTxCreator));

    auto pullTxCreator = std::make_shared<lelantus::PullTransaction::Creator>();
    sender.m_Wallet.RegisterTransactionType(TxType::PullTransaction, std::static_pointer_cast<BaseTransaction::Creator>(pullTxCreator));

    Node node;
    NodeObserver observer([&]()
    {
        auto cursor = node.get_Processor().m_Cursor;
        if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
        {
            wallet::TxParameters parameters(GenerateTxID());

            parameters.SetParameter(TxParameterID::TransactionType, TxType::PushTransaction)
                .SetParameter(TxParameterID::IsSender, true)
                .SetParameter(TxParameterID::Amount, 3800)
                .SetParameter(TxParameterID::Fee, 1200)
                .SetParameter(TxParameterID::MyID, sender.m_WalletID)
                .SetParameter(TxParameterID::Lifetime, kDefaultTxLifetime)
                .SetParameter(TxParameterID::PeerResponseTime, kDefaultTxResponseTime)
                .SetParameter(TxParameterID::CreateTime, getTimestamp());

            sender.m_Wallet.StartTransaction(parameters);
        }
        else if (cursor.m_Sid.m_Height == 30)
        {
            wallet::TxParameters parameters(GenerateTxID());

            parameters.SetParameter(TxParameterID::TransactionType, TxType::PushTransaction)
                .SetParameter(TxParameterID::IsSender, true)
                .SetParameter(TxParameterID::Amount, 7800)
                .SetParameter(TxParameterID::Fee, 1200)
                .SetParameter(TxParameterID::MyID, sender.m_WalletID)
                .SetParameter(TxParameterID::Lifetime, kDefaultTxLifetime)
                .SetParameter(TxParameterID::PeerResponseTime, kDefaultTxResponseTime)
                .SetParameter(TxParameterID::CreateTime, getTimestamp());

            sender.m_Wallet.StartTransaction(parameters);
        }
        else if (cursor.m_Sid.m_Height == 40)
        {
            wallet::TxParameters parameters(GenerateTxID());

            parameters.SetParameter(TxParameterID::TransactionType, TxType::PullTransaction)
                .SetParameter(TxParameterID::IsSender, false)
                .SetParameter(TxParameterID::Amount, 6600)
                .SetParameter(TxParameterID::Fee, 1200)
                .SetParameter(TxParameterID::MyID, sender.m_WalletID)
                .SetParameter(TxParameterID::Lifetime, kDefaultTxLifetime)
                .SetParameter(TxParameterID::PeerResponseTime, kDefaultTxResponseTime)
                .SetParameter(TxParameterID::WindowBegin, 0U)
                .SetParameter(TxParameterID::ShieldedOutputId, 1U)
                .SetParameter(TxParameterID::CreateTime, getTimestamp());

            sender.m_Wallet.StartTransaction(parameters);
        }
        else if (cursor.m_Sid.m_Height == 50)
        {
            wallet::TxParameters parameters(GenerateTxID());

            parameters.SetParameter(TxParameterID::TransactionType, TxType::PullTransaction)
                .SetParameter(TxParameterID::IsSender, false)
                .SetParameter(TxParameterID::Amount, 2600)
                .SetParameter(TxParameterID::Fee, 1200)
                .SetParameter(TxParameterID::MyID, sender.m_WalletID)
                .SetParameter(TxParameterID::Lifetime, kDefaultTxLifetime)
                .SetParameter(TxParameterID::PeerResponseTime, kDefaultTxResponseTime)
                .SetParameter(TxParameterID::WindowBegin, 0U)
                .SetParameter(TxParameterID::ShieldedOutputId, 0U)
                .SetParameter(TxParameterID::CreateTime, getTimestamp());

            sender.m_Wallet.StartTransaction(parameters);
        }
        else if (cursor.m_Sid.m_Height == 70)
        {
            WALLET_CHECK(completedCount == 0);
            mainReactor->stop();
        }
    });

    InitOwnNodeToTest(node, binaryTreasury, &observer, sender.m_WalletDB->get_MasterKdf(), 32125, 200);

    mainReactor->run();
}

void TestManyTransactons()
{
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    //int completedCount = 2;
    auto completeAction = [/*&mainReactor, &completedCount*/](auto)
    {
        /*--completedCount;
        if (completedCount == 0)
        {
            mainReactor->stop();
        }*/
    };

    const size_t kAmount = 4000;
    //const size_t kAmount = 2;
    Amount kNominalCoin = 5000;
    AmountList testAmount;

    for (size_t i = 0; i < kAmount; i++)
    {
        testAmount.push_back(kNominalCoin);
    }

    auto senderWalletDB = createSenderWalletDB(0, 0);
    //auto binaryTreasury = createTreasury(senderWalletDB, kDefaultTestAmounts);
    auto binaryTreasury = createTreasury(senderWalletDB, testAmount);
    TestWalletRig sender("sender", senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);

    auto pushTxCreator = std::make_shared<lelantus::PushTransaction::Creator>();
    sender.m_Wallet.RegisterTransactionType(TxType::PushTransaction, std::static_pointer_cast<BaseTransaction::Creator>(pushTxCreator));

    auto pullTxCreator = std::make_shared<lelantus::PullTransaction::Creator>();
    sender.m_Wallet.RegisterTransactionType(TxType::PullTransaction, std::static_pointer_cast<BaseTransaction::Creator>(pullTxCreator));

    Node node;
    NodeObserver observer([&]()
    {
        auto cursor = node.get_Processor().m_Cursor;
        if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
        {
            for (size_t i = 0; i < 500; i++)
            {
                wallet::TxParameters parameters(GenerateTxID());

                parameters.SetParameter(TxParameterID::TransactionType, TxType::PushTransaction)
                    .SetParameter(TxParameterID::Amount, 3800)
                    .SetParameter(TxParameterID::Fee, 1200)
                    .SetParameter(TxParameterID::MyID, sender.m_WalletID)
                    .SetParameter(TxParameterID::Lifetime, kDefaultTxLifetime)
                    .SetParameter(TxParameterID::PeerResponseTime, kDefaultTxResponseTime)
                    .SetParameter(TxParameterID::CreateTime, getTimestamp());

                sender.m_Wallet.StartTransaction(parameters);
            }
        }
        else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 4)
        {
            for (size_t i = 0; i < 500; i++)
            {
                wallet::TxParameters parameters(GenerateTxID());

                parameters.SetParameter(TxParameterID::TransactionType, TxType::PushTransaction)
                    .SetParameter(TxParameterID::Amount, 3800)
                    .SetParameter(TxParameterID::Fee, 1200)
                    .SetParameter(TxParameterID::MyID, sender.m_WalletID)
                    .SetParameter(TxParameterID::Lifetime, kDefaultTxLifetime)
                    .SetParameter(TxParameterID::PeerResponseTime, kDefaultTxResponseTime)
                    .SetParameter(TxParameterID::CreateTime, getTimestamp());

                sender.m_Wallet.StartTransaction(parameters);
            }
        }
        else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 5)
        {
            for (size_t i = 0; i < 500; i++)
            {
                wallet::TxParameters parameters(GenerateTxID());

                parameters.SetParameter(TxParameterID::TransactionType, TxType::PushTransaction)
                    .SetParameter(TxParameterID::Amount, 3800)
                    .SetParameter(TxParameterID::Fee, 1200)
                    .SetParameter(TxParameterID::MyID, sender.m_WalletID)
                    .SetParameter(TxParameterID::Lifetime, kDefaultTxLifetime)
                    .SetParameter(TxParameterID::PeerResponseTime, kDefaultTxResponseTime)
                    .SetParameter(TxParameterID::CreateTime, getTimestamp());

                sender.m_Wallet.StartTransaction(parameters);
            }
        }
        else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 6)
        {
            for (size_t i = 0; i < 500; i++)
            {
                wallet::TxParameters parameters(GenerateTxID());

                parameters.SetParameter(TxParameterID::TransactionType, TxType::PushTransaction)
                    .SetParameter(TxParameterID::Amount, 3800)
                    .SetParameter(TxParameterID::Fee, 1200)
                    .SetParameter(TxParameterID::MyID, sender.m_WalletID)
                    .SetParameter(TxParameterID::Lifetime, kDefaultTxLifetime)
                    .SetParameter(TxParameterID::PeerResponseTime, kDefaultTxResponseTime)
                    .SetParameter(TxParameterID::CreateTime, getTimestamp());

                sender.m_Wallet.StartTransaction(parameters);
            }
        }
        else if (cursor.m_Sid.m_Height == 150)
        {
            //WALLET_CHECK(completedCount == 0);
            mainReactor->stop();
        }
    });

    InitOwnNodeToTest(node, binaryTreasury, &observer, sender.m_WalletDB->get_MasterKdf(), 32125, 200);

    mainReactor->run();
}

int main()
{
    int logLevel = LOG_LEVEL_DEBUG;
    auto logger = beam::Logger::create(logLevel, logLevel);
    Rules::get().FakePoW = true;
    Rules::get().UpdateChecksum();
    Height fork1Height = 10;
    Height fork2Height = 20;
    Rules::get().pForks[1].m_Height = fork1Height;
    Rules::get().pForks[2].m_Height = fork2Height;

    TestSimpleTx();

    //TestManyTransactons();

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}