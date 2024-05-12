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
#include "wallet/core/common_utils.h"
#include "wallet/core/wallet_network.h"
#include "wallet/core/wallet.h"
#include "wallet/transactions/lelantus/push_transaction.h"
#include "wallet/transactions/lelantus/pull_transaction.h"
#include "wallet/transactions/lelantus/unlink_transaction.h"
#include "keykeeper/local_private_key_keeper.h"
#include "wallet/core/simple_transaction.h"
#include "core/unittest/mini_blockchain.h"
#include "utility/test_helpers.h"

#include "node/node.h"

#include "test_helpers.h"
#include "wallet_test_node.h"

#include <boost/filesystem.hpp>

using namespace beam;
using namespace std;
using namespace ECC;

WALLET_TEST_INIT

#include "wallet_test_environment.cpp"

namespace
{
    const AmountList kDefaultTestAmounts = { 50000000, 20000000, 10000000, 90000000 };

    class ScopedGlobalRules
    {
    public:
        ScopedGlobalRules()
        {
            m_rules = Rules::get();
        }

        ~ScopedGlobalRules()
        {
            Rules::get() = m_rules;
        }
    private:
        Rules m_rules;
    };

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

void TestMaxPrivacyAndOffline()
{
    cout << "\nTest sequential maxPrivacy and offline transactions\n";
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    int completedCount = 2;
    auto completeAction = [&mainReactor, &completedCount](auto)
    {
        --completedCount;
        if (completedCount == 0)
        {
            mainReactor->stop();
        }
    };

    auto senderWalletDB = createSenderWalletDB(0, 0, false, true);
    auto binaryTreasury = createTreasury(senderWalletDB, kDefaultTestAmounts);
    TestWalletRig sender(senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);
    auto receiverWalletDB = createReceiverWalletDB(false, true);
    TestWalletRig receiver(receiverWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);

    sender.m_Wallet->RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>([=]() {return senderWalletDB; }));
    receiver.m_Wallet->RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>([=]() {return receiverWalletDB; }));

    TxID txID = {}, txID2 = {};
    Node node;
    bool bTxCreated = false;
    NodeObserver observer([&]()
    {
        const auto& cursor = node.get_Processor().m_Cursor;
        if (!bTxCreated && (cursor.m_Sid.m_Height >= Rules::get().pForks[2].m_Height + 3))
        {
            auto maxPrivacyToken = GenerateTokenDefaultAddr(TokenType::MaxPrivacy, receiver.m_WalletDB);
            auto offlineToken = GenerateTokenDefaultAddr(TokenType::Offline, receiver.m_WalletDB, 1);

            auto f = [&](const std::string& token, TxAddressType t)->TxID
            {
                auto p = ParseParameters(token);
                WALLET_CHECK(p);
                WALLET_CHECK(t == GetAddressType(token));
                auto parameters = lelantus::CreatePushTransactionParameters()
                    .SetParameter(TxParameterID::Amount, 18000000)
                    .SetParameter(TxParameterID::Fee, 12000000);
                LoadReceiverParams(*p, parameters, t);

                return sender.m_Wallet->StartTransaction(parameters);
            };

            txID = f(maxPrivacyToken, TxAddressType::MaxPrivacy);
            txID2 = f(offlineToken, TxAddressType::Offline);

            bTxCreated = true;
        }
        else if (cursor.m_Sid.m_Height == 50)
        {
            mainReactor->stop();
        }
    });

    InitOwnNodeToTest(node, binaryTreasury, &observer, receiver.m_WalletDB->get_MasterKdf(), 32125, 200);

    mainReactor->run();

    WALLET_CHECK(completedCount == 0);
    {
        auto txHistory = sender.m_WalletDB->getTxHistory(TxType::ALL);
        WALLET_CHECK(txHistory.size() == 2);
        WALLET_CHECK(txHistory[0].m_txType == TxType::PushTransaction && txHistory[0].m_status == TxStatus::Completed);
        WALLET_CHECK(txHistory[1].m_txType == TxType::PushTransaction && txHistory[1].m_status == TxStatus::Completed);
    }

    {
        auto txHistory = receiver.m_WalletDB->getTxHistory(TxType::ALL);
        WALLET_CHECK(txHistory.size() == 2);
        WALLET_CHECK(txHistory[0].m_txType == TxType::PushTransaction && txHistory[0].m_status == TxStatus::Completed);
        WALLET_CHECK(txHistory[1].m_txType == TxType::PushTransaction && txHistory[1].m_status == TxStatus::Completed);

        auto shieldedCoins = receiver.m_WalletDB->getShieldedCoins(Asset::Asset::s_BeamID);
        WALLET_CHECK(shieldedCoins.size() == 2);
        WALLET_CHECK(shieldedCoins[0].m_CoinID.m_Value == 18000000);
        WALLET_CHECK(shieldedCoins[0].m_CoinID.m_Key.m_IsCreatedByViewer == true);
        WALLET_CHECK(shieldedCoins[0].m_Status == ShieldedCoin::Status::Maturing);
        WALLET_CHECK(shieldedCoins[1].m_CoinID.m_Value == 18000000);
        WALLET_CHECK(shieldedCoins[1].m_CoinID.m_Key.m_IsCreatedByViewer == true);
        //WALLET_CHECK(shieldedCoins[1].m_Status == ShieldedCoin::Status::Available);
    }
}

void TestTreasuryRestore()
{
    cout << "\nTest tresury restore\n";
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    int completedCount = 1;
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
    TestWalletRig sender(senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);
    auto receiverWalletDB = createReceiverWalletDB(false, true);
    TestWalletRig receiver(receiverWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);
    sender.m_Wallet->RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>([=]() { return senderWalletDB; }));
    receiver.m_Wallet->RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>([=]() { return receiverWalletDB; }));

    sender.m_Wallet->Rescan();
    Node node;
    NodeObserver observer([&]()
    {
        auto cursor = node.get_Processor().m_Cursor;
        if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
        {
            WalletAddress walletAddress;
            receiver.m_WalletDB->getDefaultAddressAlways(walletAddress);

            auto newAddress = GenerateOfflineToken(walletAddress, *receiver.m_WalletDB, 0, Asset::s_BeamID, "");
            auto p = ParseParameters(newAddress);
            WALLET_CHECK(p);
            auto parameters = lelantus::CreatePushTransactionParameters()
                .SetParameter(TxParameterID::Amount, 38000000)
                .SetParameter(TxParameterID::Fee, 12000000);

            LoadReceiverParams(*p, parameters, TxAddressType::Offline);

            sender.m_Wallet->StartTransaction(parameters);
        }

        else if (cursor.m_Sid.m_Height == 40)
        {
            mainReactor->stop();
        }
    });

    InitOwnNodeToTest(node, binaryTreasury, &observer, receiver.m_WalletDB->get_MasterKdf(), 32125, 200); // node detects receiver events

    mainReactor->run();

    WALLET_CHECK(completedCount == 0);
    {
        auto txHistory = sender.m_WalletDB->getTxHistory(TxType::ALL);
        WALLET_CHECK(txHistory.size() == 1);
        WALLET_CHECK(txHistory[0].m_txType == TxType::PushTransaction && txHistory[0].m_status == TxStatus::Completed);
        auto coins = sender.GetCoins();
        WALLET_CHECK(coins.size() == 4);
        std::sort(coins.begin(), coins.end(), [](const Coin& left, const Coin& right) {return left.m_ID.m_Value < right.m_ID.m_Value; });

        WALLET_CHECK(coins[0].m_ID.m_Value == 10000000);
        WALLET_CHECK(coins[1].m_ID.m_Value == 20000000);
        WALLET_CHECK(coins[2].m_ID.m_Value == 50000000);
        WALLET_CHECK(coins[3].m_ID.m_Value == 90000000);
    }

    {
        auto txHistory = receiver.m_WalletDB->getTxHistory(TxType::ALL);
        WALLET_CHECK(txHistory.size() == 1);
        WALLET_CHECK(txHistory[0].m_txType == TxType::PushTransaction && txHistory[0].m_status == TxStatus::Completed);
        auto shieldedCoins = receiver.m_WalletDB->getShieldedCoins(Asset::Asset::s_BeamID);
        WALLET_CHECK(shieldedCoins[0].m_CoinID.m_Value == 38000000);
        WALLET_CHECK(shieldedCoins[0].m_CoinID.m_Key.m_IsCreatedByViewer == true);

        auto nStatus = shieldedCoins[0].m_Status;
        cout << "@@@ coin status: " << static_cast<uint32_t>(nStatus) << std::endl;
        WALLET_CHECK((ShieldedCoin::Status::Available == nStatus) || (ShieldedCoin::Status::Maturing == nStatus));
        
    }
}

void TestRestoreInterruption()
{
    cout << "\nTest restore interruption\n";
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    const int totalCoins = 100;
    const int txNum = 20;
    int completedCount = txNum;
    auto completeAction = [&mainReactor, &completedCount](auto)
    {
        --completedCount;
        if (completedCount == 0)
        {
            mainReactor->stop();
        }
    };

    auto senderWalletDB = createSenderWalletDB(0, 0);
    AmountList amounts(totalCoins, Amount(50000000) );
    auto binaryTreasury = createTreasury(senderWalletDB, amounts);
    TestWalletRig sender(senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);
    auto receiverWalletDB = createReceiverWalletDB(false, true);
    TestWalletRig receiver(receiverWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);
    receiver.m_Wallet->EnableBodyRequests(true);

    auto minerWalletDB = createSqliteWalletDB("minerWallet.db", false, true);
    TestWalletRig miner(minerWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);
    sender.m_Wallet->RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>([=]() { return senderWalletDB; }));
    receiver.m_Wallet->RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>([=]() { return receiverWalletDB; }));
    miner.m_Wallet->RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>([=]() { return minerWalletDB; }));

    sender.m_Wallet->Rescan();
    Node node;
    NodeObserver observer([&]()
    {
        auto cursor = node.get_Processor().m_Cursor;
        if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
        {
            for (int i = 0; i < txNum; ++i)
            {
                WalletAddress walletAddress;
                receiver.m_WalletDB->getDefaultAddressAlways(walletAddress);

                auto newAddress = GenerateOfflineToken(walletAddress, *receiver.m_WalletDB, 0, Asset::s_BeamID, "");
                auto p = ParseParameters(newAddress);
                WALLET_CHECK(p);
                auto parameters = lelantus::CreatePushTransactionParameters()
                    .SetParameter(TxParameterID::Amount, 38000000)
                    .SetParameter(TxParameterID::Fee, 12000000);

                LoadReceiverParams(*p, parameters, TxAddressType::Offline);

                sender.m_Wallet->StartTransaction(parameters);
            }
            
        }

        else if (cursor.m_Sid.m_Height == 40)
        {
            mainReactor->stop();
        }
    });
    
    InitOwnNodeToTest(node, binaryTreasury, &observer, miner.m_WalletDB->get_OwnerKdf(), 32125, 200);

    mainReactor->run();

    WALLET_CHECK(completedCount == 0);
    {
        auto txHistory = sender.m_WalletDB->getTxHistory(TxType::ALL);
        WALLET_CHECK(txHistory.size() == txNum);
        WALLET_CHECK(std::all_of(txHistory.begin(), txHistory.end(), [](const auto& v) { return v.m_txType == TxType::PushTransaction && v.m_status == TxStatus::Completed; }));
        auto coins = sender.GetCoins();
        WALLET_CHECK(coins.size() == totalCoins);
        std::sort(coins.begin(), coins.end(), [](const Coin& left, const Coin& right) {return left.m_ID.m_Value < right.m_ID.m_Value; });

    }

    {
        auto txHistory = receiver.m_WalletDB->getTxHistory(TxType::ALL);
        WALLET_CHECK(txHistory.size() == txNum);
      
    }


    struct InterruptObserver : public wallet::IWalletObserver
    {
        void onSyncProgress(int done, int total) override
        { 
            if (done == total && total == 0)
            {
                std::cout << "Sync completed" << std::endl;
                io::Reactor::get_Current().stop();
                return;
            }
            int p = static_cast<int>((done * 100) / total);
            if (p >= 50)
            {
                std::cout << "Interrupting on " << p << "%" << std::endl;
                io::Reactor::get_Current().stop();
            }
        }
    };

    struct SyncObserver : public wallet::IWalletObserver
    {
        void onSyncProgress(int done, int total) override
        {
            if (done == total && total == 0)
            {
                std::cout << "Sync completed" << std::endl;
                io::Reactor::get_Current().stop();
            }
        }
    };

    InterruptObserver ob;
    receiver.m_Wallet->Subscribe(&ob);
    receiver.m_Wallet->Rescan();
    mainReactor->run();
    receiver.m_Wallet->Unsubscribe(&ob);
    SyncObserver ob2;
    receiver.m_Wallet->Subscribe(&ob2);
    std::cout << "Resuming..." << std::endl;
    mainReactor->run();
    receiver.m_Wallet->Unsubscribe(&ob2);
    {
        auto txHistory = receiver.m_WalletDB->getTxHistory(TxType::ALL);
        WALLET_CHECK(txHistory.size() == txNum);
        WALLET_CHECK(txHistory[0].m_txType == TxType::PushTransaction && txHistory[0].m_status == TxStatus::Completed);
        auto shieldedCoins = receiver.m_WalletDB->getShieldedCoins(Asset::Asset::s_BeamID);
        WALLET_CHECK(shieldedCoins.size() == txNum);
        for (const auto& sc : shieldedCoins)
        {
            WALLET_CHECK(sc.m_CoinID.m_Value == 38000000);
            WALLET_CHECK(sc.m_CoinID.m_Key.m_IsCreatedByViewer == true);
            WALLET_CHECK(sc.m_Status == ShieldedCoin::Status::Available);
        }
    }
    completedCount = 2;
    receiver.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
        .SetParameter(TxParameterID::PeerAddr, sender.m_BbsAddr)
        .SetParameter(TxParameterID::Amount, Amount(txNum * 37000000))
        .SetParameter(TxParameterID::Fee, Amount(1000000))
        .SetParameter(TxParameterID::Lifetime, Height(200))
        .SetParameter(TxParameterID::PeerResponseTime, Height(20)));

    mainReactor->run();

    {
        auto txHistory = receiver.m_WalletDB->getTxHistory(TxType::ALL);
        WALLET_CHECK(txHistory.size() == txNum+1);
        auto it = std::find_if(txHistory.begin(), txHistory.end(), [](const auto& v) {return v.m_txType == TxType::Simple; });
        WALLET_CHECK(it != txHistory.end());
        WALLET_CHECK(it->m_status == TxStatus::Completed);
        auto shieldedCoins = receiver.m_WalletDB->getShieldedCoins(Asset::Asset::s_BeamID);
        WALLET_CHECK(shieldedCoins.size() == txNum);
        WALLET_CHECK(std::all_of(shieldedCoins.begin(), shieldedCoins.end(), [](const auto& v) {return v.m_Status == ShieldedCoin::Status::Spent; }));
    }
}

void TestSimpleTx()
{
    cout << "\nTest simple lelantus tx's: 2 pushTx and 2 pullTx\n";
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    int completedCount = 2;
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
    TestWalletRig sender(senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);

    sender.m_Wallet->RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>([=](){return senderWalletDB;}));
    sender.m_Wallet->RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());

    Node node;
    NodeObserver observer([&]()
    {
        auto cursor = node.get_Processor().m_Cursor;
        if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
        {
            auto parameters = lelantus::CreatePushTransactionParameters()
                .SetParameter(TxParameterID::Amount, 38000000)
                .SetParameter(TxParameterID::Fee, 12000000); 

            sender.m_Wallet->StartTransaction(parameters);
        }
        else if (cursor.m_Sid.m_Height == 30)
        {
            auto parameters = lelantus::CreatePushTransactionParameters()
                .SetParameter(TxParameterID::Amount, 78000000)
                .SetParameter(TxParameterID::Fee, 12000000);

            sender.m_Wallet->StartTransaction(parameters);
        }
        //else if (cursor.m_Sid.m_Height == 40)
        //{
        //    auto parameters = lelantus::CreatePullTransactionParameters()
        //        .SetParameter(TxParameterID::Amount, 66000000)
        //        .SetParameter(TxParameterID::AmountList, AmountList{ 46000000, 20000000 })
        //        .SetParameter(TxParameterID::Fee, 12000000)
        //        .SetParameter(TxParameterID::ShieldedOutputId, 1U);
        //
        //    sender.m_Wallet->StartTransaction(parameters);
        //}
        //else if (cursor.m_Sid.m_Height == 50)
        //{
        //    auto parameters = lelantus::CreatePullTransactionParameters()
        //        .SetParameter(TxParameterID::Amount, 26000000)
        //        .SetParameter(TxParameterID::Fee, 12000000)
        //        .SetParameter(TxParameterID::ShieldedOutputId, 0U);
        //
        //    sender.m_Wallet->StartTransaction(parameters);
        //}
        else if (cursor.m_Sid.m_Height == 70)
        {
            mainReactor->stop();
        }
    });

    InitOwnNodeToTest(node, binaryTreasury, &observer, sender.m_WalletDB->get_MasterKdf(), 32125, 200);

    mainReactor->run();

    WALLET_CHECK(completedCount == 0);
    auto txHistory = sender.m_WalletDB->getTxHistory(TxType::ALL);
    WALLET_CHECK(std::all_of(txHistory.begin(), txHistory.end(), [](const auto& tx)
        {
            return (tx.m_txType == TxType::PushTransaction || tx.m_txType == TxType::PullTransaction) && tx.m_status == TxStatus::Completed;
        }));
}

void TestMaxPrivacyTx()
{
    cout << "\nTest maxPrivacy lelantus tx via public address\n";
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    int completedCount = 1;
    auto completeAction = [&mainReactor, &completedCount](auto)
    {
        --completedCount;
        if (completedCount == 0)
        {
            mainReactor->stop();
        }
    };

    auto senderWalletDB = createSenderWalletDB(0, 0, false, true);
    auto binaryTreasury = createTreasury(senderWalletDB, kDefaultTestAmounts);
    TestWalletRig sender(senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);
    auto receiverWalletDB = createReceiverWalletDB(false, true);
    TestWalletRig receiver(receiverWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);

    sender.m_Wallet->RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>([=](){return senderWalletDB;}));

    receiver.m_Wallet->RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());

    IPrivateKeyKeeper2::Method::CreateVoucherShielded m;
    m.m_iEndpoint = receiver.m_OwnID;
    m.m_Count = 1;
    ECC::GenRandom(m.m_Nonce);
    WALLET_CHECK(receiver.m_WalletDB->get_KeyKeeper()->InvokeSync(m) == IPrivateKeyKeeper2::Status::Success);
    WALLET_CHECK(!m.m_Res.empty());


    TxID txID = {};
    Node node;
    NodeObserver observer([&]()
    {
        auto cursor = node.get_Processor().m_Cursor;
        if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
        {
            auto parameters = lelantus::CreatePushTransactionParameters()
                .SetParameter(TxParameterID::Amount, 18000000)
                .SetParameter(TxParameterID::Fee, 12000000)
                .SetParameter(TxParameterID::Voucher, m.m_Res.front()) // preassing the voucher
                .SetParameter(TxParameterID::PeerEndpoint, receiver.m_Endpoint)
                .SetParameter(TxParameterID::MaxPrivacyMinAnonimitySet, uint8_t(64));

            txID = sender.m_Wallet->StartTransaction(parameters);
        }
        else if (cursor.m_Sid.m_Height == 50)
        {
            mainReactor->stop();
        }
    });

    InitOwnNodeToTest(node, binaryTreasury, &observer, receiver.m_WalletDB->get_MasterKdf(), 32125, 200);

    mainReactor->run();

    WALLET_CHECK(completedCount == 0);
    {
        auto txHistory = sender.m_WalletDB->getTxHistory(TxType::ALL);
        WALLET_CHECK(txHistory.size() == 1);
        WALLET_CHECK(txHistory[0].m_txType == TxType::PushTransaction && txHistory[0].m_status == TxStatus::Completed);
    }

    {
        auto txHistory = receiver.m_WalletDB->getTxHistory(TxType::ALL);
        WALLET_CHECK(txHistory.size() == 1);
        WALLET_CHECK(txHistory[0].m_txType == TxType::PushTransaction && txHistory[0].m_status == TxStatus::Completed);
        WALLET_CHECK(txHistory[0].m_txId == txID);
        auto shieldedCoins = receiver.m_WalletDB->getShieldedCoins(Asset::Asset::s_BeamID);
        WALLET_CHECK(shieldedCoins[0].m_CoinID.m_Value == 18000000);
        WALLET_CHECK(shieldedCoins[0].m_CoinID.m_Key.m_IsCreatedByViewer == true);
        WALLET_CHECK(shieldedCoins[0] .m_Status == ShieldedCoin::Status::Maturing);
    }
}

void TestPublicAddressTx()
{
    cout << "\nTest simple lelantus tx via public address\n";
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    int completedCount = 1;
    auto completeAction = [&mainReactor, &completedCount](auto)
    {
        --completedCount;
        if (completedCount == 0)
        {
            mainReactor->stop();
        }
    };

    auto senderWalletDB = createSenderWalletDB(0, 0, false, true);
    auto binaryTreasury = createTreasury(senderWalletDB, kDefaultTestAmounts);
    TestWalletRig sender(senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);
    auto receiverWalletDB = createReceiverWalletDB(false, true);
    TestWalletRig receiver(receiverWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);

    sender.m_Wallet->RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>([=](){return senderWalletDB;}));

    receiver.m_Wallet->RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());

    IPrivateKeyKeeper2::Method::CreateOfflineAddr mAddr;
    mAddr.m_iEndpoint = receiver.m_OwnID;
    WALLET_CHECK(receiver.m_WalletDB->get_KeyKeeper()->InvokeSync(mAddr) == IPrivateKeyKeeper2::Status::Success);

    TxID txID = {};
    Node node;
    NodeObserver observer([&]()
    {
        auto cursor = node.get_Processor().m_Cursor;
        if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
        {
            auto parameters = lelantus::CreatePushTransactionParameters()
                .SetParameter(TxParameterID::Amount, 18000000)
                .SetParameter(TxParameterID::Fee, 12000000)
                .SetParameter(TxParameterID::PublicAddreessGen, mAddr.m_Addr)
                .SetParameter(TxParameterID::PublicAddressGenSig, mAddr.m_Signature)
                .SetParameter(TxParameterID::PeerEndpoint, receiver.m_Endpoint);

            txID = sender.m_Wallet->StartTransaction(parameters);
        }
        else if (cursor.m_Sid.m_Height == 50)
        {
            mainReactor->stop();
        }
    });

    InitOwnNodeToTest(node, binaryTreasury, &observer, receiver.m_WalletDB->get_MasterKdf(), 32125, 200);

    mainReactor->run();

    WALLET_CHECK(completedCount == 0);
    {
        auto txHistory = sender.m_WalletDB->getTxHistory(TxType::ALL);
        WALLET_CHECK(txHistory.size() == 1);
        WALLET_CHECK(txHistory[0].m_txType == TxType::PushTransaction && txHistory[0].m_status == TxStatus::Completed);
    }

    {
        auto txHistory = receiver.m_WalletDB->getTxHistory(TxType::ALL);
        WALLET_CHECK(txHistory.size() == 1);
        WALLET_CHECK(txHistory[0].m_txType == TxType::PushTransaction && txHistory[0].m_status == TxStatus::Completed);
        WALLET_CHECK(txHistory[0].m_txId == txID);
        auto shieldedCoins = receiver.m_WalletDB->getShieldedCoins(Asset::Asset::s_BeamID);
        WALLET_CHECK(shieldedCoins[0].m_CoinID.m_Value == 18000000);
        WALLET_CHECK(shieldedCoins[0].m_CoinID.m_Key.m_IsCreatedByViewer == false);
    }
}

void TestDirectAnonymousPayment()
{
    cout << "\nTest direct anonimous payment with lelantus\n";
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    int completedCount = 3;
    auto completeAction = [&mainReactor, &completedCount](auto)
    {
        --completedCount;
        if (completedCount == 0)
        {
            mainReactor->stop();
        }
    };

    auto senderWalletDB = createSenderWalletDB(0, 0, false, true);
    auto binaryTreasury = createTreasury(senderWalletDB, kDefaultTestAmounts);
    TestWalletRig sender(senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);
    auto receiverWalletDB = createReceiverWalletDB(false, true);
    TestWalletRig receiver(receiverWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);

    sender.m_Wallet->RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>([=](){return senderWalletDB;}));

    receiver.m_Wallet->RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());
    receiver.m_Wallet->EnableBodyRequests(true);

    auto vouchers = GenerateVoucherList(receiver.m_WalletDB->get_KeyKeeper(), receiver.m_OwnID, 2);
    WALLET_CHECK(IsValidVoucherList(vouchers, receiver.m_Endpoint));
    WALLET_CHECK(!IsValidVoucherList(vouchers, sender.m_Endpoint));

    Node node;
    NodeObserver observer([&]()
        {
            auto cursor = node.get_Processor().m_Cursor;
            if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
            {
                auto parameters = lelantus::CreatePushTransactionParameters()
                    .SetParameter(TxParameterID::Amount, 18000000)
                    .SetParameter(TxParameterID::Fee, 12000000)
                    .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr)
                    .SetParameter(TxParameterID::Voucher, vouchers.front()) // preassing the voucher
                    .SetParameter(TxParameterID::MyEndpoint, sender.m_Endpoint)
                    .SetParameter(TxParameterID::PeerEndpoint, receiver.m_Endpoint);

                sender.m_Wallet->StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == 20)
            {
                // second attempt
                auto parameters = lelantus::CreatePushTransactionParameters()
                    .SetParameter(TxParameterID::Amount, 18000000)
                    .SetParameter(TxParameterID::Fee, 12000000)
                    .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr)
                    .SetParameter(TxParameterID::Voucher, vouchers.front()) // attempt to reuse same voucher
                    .SetParameter(TxParameterID::MyEndpoint, sender.m_Endpoint)
                    .SetParameter(TxParameterID::PeerEndpoint, receiver.m_Endpoint);
            
                sender.m_Wallet->StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == 26)
            {
                auto parameters = lelantus::CreatePushTransactionParameters()
                    .SetParameter(TxParameterID::Amount, 18000000)
                    .SetParameter(TxParameterID::Fee, 12000000)
                    .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr)
                    .SetParameter(TxParameterID::MyEndpoint, sender.m_Endpoint)
                    .SetParameter(TxParameterID::PeerEndpoint, receiver.m_Endpoint);
            
                sender.m_Wallet->StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == 50)
            {
                mainReactor->stop();
            }
        });

    // intentionally with sender owner key, receiver should get events from blocks by himself
    InitOwnNodeToTest(node, binaryTreasury, &observer, sender.m_WalletDB->get_MasterKdf(), 32125, 200);

    mainReactor->run();

    WALLET_CHECK(completedCount == 0);
    {
        auto txHistory = sender.m_WalletDB->getTxHistory(TxType::ALL);
        std::sort(txHistory.begin(), txHistory.end(), [](const auto& left, const auto& right) {return left.m_status < right.m_status; });
        WALLET_CHECK(txHistory[0].m_txType == TxType::PushTransaction && txHistory[0].m_status == TxStatus::Completed);
        WALLET_CHECK(txHistory[1].m_txType == TxType::PushTransaction && txHistory[1].m_status == TxStatus::Completed);
        WALLET_CHECK(txHistory[2].m_txType == TxType::PushTransaction && txHistory[2].m_status == TxStatus::Completed);
    }
    {
        auto txHistory = receiver.m_WalletDB->getTxHistory(TxType::ALL);
        WALLET_CHECK(txHistory.size() == 3);
        WALLET_CHECK(std::all_of(txHistory.begin(), txHistory.end(), [](const auto& tx)
        {
            return (tx.m_txType == TxType::PushTransaction) && tx.m_status == TxStatus::Completed;
        }));
        for (const auto& tx : txHistory)
        {
            if (tx.m_txType == TxType::PushTransaction)
            {
                WALLET_CHECK(tx.m_amount == 18000000);
            }
        }

        {
            TxID txID = *txHistory[0].GetTxID();

            WALLET_CHECK(txHistory[0].getSenderEndpoint() == std::to_base58(sender.m_Endpoint));
//            WALLET_CHECK(txHistory[0].getReceiverEndpoint() == std::to_base58(receiver.m_Endpoint));
            ByteBuffer b = storage::ExportPaymentProof(*sender.m_WalletDB, txID);

            WALLET_CHECK(storage::VerifyPaymentProof(b, *sender.m_WalletDB));

            auto pi2 = storage::ShieldedPaymentInfo::FromByteBuffer(b);

            TxKernel::Ptr k;
            ShieldedTxo::Voucher voucher;
            Amount amount = 0;
            Asset::ID assetID = Asset::s_InvalidID;;
            PeerID peerEndpoint = Zero;
            PeerID myEndpoint = Zero;
            bool success = true;
            success &= storage::getTxParameter(*sender.m_WalletDB, txID, TxParameterID::Kernel, k);
            WALLET_CHECK(k->get_Subtype() == TxKernel::Subtype::Std);

            auto& kernel = k->m_vNested[0]->CastTo_ShieldedOutput();
            success &= storage::getTxParameter(*sender.m_WalletDB, txID, TxParameterID::Voucher, voucher);
            success &= storage::getTxParameter(*sender.m_WalletDB, txID, TxParameterID::PeerEndpoint, peerEndpoint);
            success &= storage::getTxParameter(*sender.m_WalletDB, txID, TxParameterID::MyEndpoint, myEndpoint);
            success &= storage::getTxParameter(*sender.m_WalletDB, txID, TxParameterID::Amount, amount);
            storage::getTxParameter(*sender.m_WalletDB, txID, TxParameterID::AssetID, assetID);

            WALLET_CHECK(pi2.m_KernelID == k->m_Internal.m_ID);

            WALLET_CHECK(success);
            {
                ShieldedTxo::Voucher voucher2;
                voucher2.m_SharedSecret = voucher.m_SharedSecret;
                voucher2.m_Signature = voucher.m_Signature;
                voucher2.m_Ticket = kernel.m_Txo.m_Ticket;

                WALLET_CHECK(voucher2.IsValid(peerEndpoint));

                ECC::Oracle oracle;
                oracle << kernel.m_Msg;
                ShieldedTxo::Data::OutputParams outputParams;
                WALLET_CHECK(outputParams.Recover(kernel.m_Txo, voucher.m_SharedSecret, k->m_Height.m_Min, oracle));
                WALLET_CHECK(outputParams.m_Value == amount);
                WALLET_CHECK(outputParams.m_AssetID == assetID);
                WALLET_CHECK(outputParams.m_User.m_Sender == myEndpoint);
            }
        }
    }
    
    {
        // TODO: commented PullTransaction is outdated doesn't work
        //auto txHistory = receiver.m_WalletDB->getTxHistory(TxType::ALL);
        //WALLET_CHECK(std::all_of(txHistory.begin(), txHistory.end(), [](const auto& tx)
        //    {
        //        return (tx.m_txType == TxType::PullTransaction) && tx.m_status == TxStatus::Completed;
        //    }));
        //for (const auto& tx : txHistory)
        //{
        //    if (tx.m_txType == TxType::PullTransaction)
        //    {
        //        auto coins = receiver.m_WalletDB->getCoinsCreatedByTx(tx.m_txId);
        //        WALLET_CHECK(coins.size() == 1);
        //        WALLET_CHECK(coins[0].getAmount() == 6000000);
        //    }
        //}
    }
}

void TestManyTransactons()
{
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    int completedCount = 20000000;
    auto completeAction = [&mainReactor, &completedCount](auto)
    {
        --completedCount;
        if (completedCount == 0)
        {
            mainReactor->stop();
        }
    };

    constexpr size_t kAmount = 20000000;
    constexpr Amount kNominalCoin = 50000000;
    AmountList testAmount(kAmount, kNominalCoin);

    auto senderWalletDB = createSenderWalletDB(0, 0);
    //auto binaryTreasury = createTreasury(senderWalletDB, kDefaultTestAmounts);
    auto binaryTreasury = createTreasury(senderWalletDB, testAmount);
    TestWalletRig sender(senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);

    sender.m_Wallet->RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>([=](){return senderWalletDB;}));

    sender.m_Wallet->RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());

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
                    .SetParameter(TxParameterID::Amount, 38000000)
                    .SetParameter(TxParameterID::Fee, 12000000)
                    .SetParameter(TxParameterID::Lifetime, kDefaultTxLifetime)
                    .SetParameter(TxParameterID::PeerResponseTime, kDefaultTxResponseTime)
                    .SetParameter(TxParameterID::CreateTime, getTimestamp());

                sender.m_Wallet->StartTransaction(parameters);
            }
        }
        else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 4)
        {
            for (size_t i = 0; i < 500; i++)
            {
                wallet::TxParameters parameters(GenerateTxID());

                parameters.SetParameter(TxParameterID::TransactionType, TxType::PushTransaction)
                    .SetParameter(TxParameterID::Amount, 38000000)
                    .SetParameter(TxParameterID::Fee, 12000000)
                    .SetParameter(TxParameterID::Lifetime, kDefaultTxLifetime)
                    .SetParameter(TxParameterID::PeerResponseTime, kDefaultTxResponseTime)
                    .SetParameter(TxParameterID::CreateTime, getTimestamp());

                sender.m_Wallet->StartTransaction(parameters);
            }
        }
        else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 5)
        {
            for (size_t i = 0; i < 500; i++)
            {
                wallet::TxParameters parameters(GenerateTxID());

                parameters.SetParameter(TxParameterID::TransactionType, TxType::PushTransaction)
                    .SetParameter(TxParameterID::Amount, 38000000)
                    .SetParameter(TxParameterID::Fee, 12000000)
                    .SetParameter(TxParameterID::Lifetime, kDefaultTxLifetime)
                    .SetParameter(TxParameterID::PeerResponseTime, kDefaultTxResponseTime)
                    .SetParameter(TxParameterID::CreateTime, getTimestamp());

                sender.m_Wallet->StartTransaction(parameters);
            }
        }
        else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 6)
        {
            for (size_t i = 0; i < 500; i++)
            {
                wallet::TxParameters parameters(GenerateTxID());

                parameters.SetParameter(TxParameterID::TransactionType, TxType::PushTransaction)
                    .SetParameter(TxParameterID::Amount, 38000000)
                    .SetParameter(TxParameterID::Fee, 12000000)
                    .SetParameter(TxParameterID::Lifetime, kDefaultTxLifetime)
                    .SetParameter(TxParameterID::PeerResponseTime, kDefaultTxResponseTime)
                    .SetParameter(TxParameterID::CreateTime, getTimestamp());

                sender.m_Wallet->StartTransaction(parameters);
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

void TestShortWindow()
{
    // save defaults
    ScopedGlobalRules rules;

    Rules::get().Shielded.m_ProofMax = { 2, 6 }; // 64
    Rules::get().Shielded.m_ProofMin = { 2, 4 }; // 16
    Rules::get().Shielded.MaxWindowBacklog = 64;

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    constexpr size_t kCount = 200;
    constexpr size_t kSplitTxCount = 1;
    constexpr size_t kExtractShieldedTxCount = 5;

    int completedCount = kSplitTxCount + kCount + kExtractShieldedTxCount;
    auto completeAction = [&mainReactor, &completedCount](auto)
    {
        --completedCount;
        if (completedCount == 0)
        {
            mainReactor->stop();
        }
    };

    constexpr Amount kCoinAmount = 40000000;
    constexpr Amount kFee = 20000000;
    constexpr Amount kNominalCoin = kCoinAmount + kFee;
    AmountList testAmount(kCount, kNominalCoin);

    auto senderWalletDB = createSenderWalletDB(0, 0);
    // Coin for split TX
    auto binaryTreasury = createTreasury(senderWalletDB, { (kCount + 1) * kNominalCoin });
    TestWalletRig sender(senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);


    sender.m_Wallet->RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>([=](){return senderWalletDB;}));

    sender.m_Wallet->RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());

    Node node;
    NodeObserver observer([&]()
        {
            auto cursor = node.get_Processor().m_Cursor;
            // create 300(kCount) coins(split TX)
            if (cursor.m_Sid.m_Height == 3)
            {
                auto splitTxParameters = CreateSplitTransactionParameters(testAmount)
                    .SetParameter(TxParameterID::Fee, Amount(kNominalCoin));

                sender.m_Wallet->StartTransaction(splitTxParameters);
            }
            // insert 300(kCount) coins to shielded pool
            else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
            {
                for (size_t i = 0; i < kCount; i++)
                {
                    auto parameters = lelantus::CreatePushTransactionParameters()
                        .SetParameter(TxParameterID::Amount, kCoinAmount)
                        .SetParameter(TxParameterID::Fee, kFee);

                    sender.m_Wallet->StartTransaction(parameters);
                }
            }
            // extract one of first shielded UTXO
            else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 15)
            {
                {
                    auto parameters = lelantus::CreatePullTransactionParameters()
                        .SetParameter(TxParameterID::Amount, kCoinAmount - kFee)
                        .SetParameter(TxParameterID::Fee, kFee)
                        .SetParameter(TxParameterID::ShieldedOutputId, 40U);

                    sender.m_Wallet->StartTransaction(parameters);
                }
                {
                    auto parameters = lelantus::CreatePullTransactionParameters()
                        .SetParameter(TxParameterID::Amount, kCoinAmount - kFee)
                        .SetParameter(TxParameterID::Fee, kFee)
                        .SetParameter(TxParameterID::ShieldedOutputId, 42U);

                    sender.m_Wallet->StartTransaction(parameters);
                }
                {
                    auto parameters = lelantus::CreatePullTransactionParameters()
                        .SetParameter(TxParameterID::Amount, kCoinAmount - kFee)
                        .SetParameter(TxParameterID::Fee, kFee)
                        .SetParameter(TxParameterID::ShieldedOutputId, 43U);

                    sender.m_Wallet->StartTransaction(parameters);
                }
            }
            else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 20)
            {
                auto parameters = lelantus::CreatePullTransactionParameters()
                    .SetParameter(TxParameterID::Amount, kCoinAmount - kFee)
                    .SetParameter(TxParameterID::Fee, kFee)
                    .SetParameter(TxParameterID::ShieldedOutputId, 62U);

                sender.m_Wallet->StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 30)
            {
                auto parameters = lelantus::CreatePullTransactionParameters()
                    .SetParameter(TxParameterID::Amount, kCoinAmount - kFee)
                    .SetParameter(TxParameterID::Fee, kFee)
                    .SetParameter(TxParameterID::ShieldedOutputId, 180);

                sender.m_Wallet->StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 40)
            {
                mainReactor->stop();
            }
        });

    InitOwnNodeToTest(node, binaryTreasury, &observer, sender.m_WalletDB->get_MasterKdf(), 32125, 200);

    mainReactor->run();

    auto txHistory = sender.m_WalletDB->getTxHistory(TxType::PullTransaction);

    WALLET_CHECK(completedCount == 0);
    WALLET_CHECK(txHistory.size() == kExtractShieldedTxCount);
    WALLET_CHECK(std::all_of(txHistory.begin(), txHistory.end(), [](const auto& tx) { return tx.m_status == TxStatus::Completed;}));
}

void TestManyTransactons(const uint32_t txCount, Lelantus::Cfg cfg = Lelantus::Cfg{4,3}, Lelantus::Cfg minCfg = Lelantus::Cfg{4,2})
{
    cout << "\nTest " << txCount << " pushTx's and " << txCount << " pullTx's, Cfg: n = " << cfg.n << " M = " << cfg.M << " , minCfg: n = " << minCfg.n << " M = " << minCfg.M << "\n";

    // save defaults
    ScopedGlobalRules rules;

    Rules::get().Shielded.m_ProofMax = cfg;
    Rules::get().Shielded.m_ProofMin = minCfg;
    Rules::get().Shielded.MaxWindowBacklog = cfg.get_N() + 200;
    //uint32_t minBlocksToCompletePullTxs = txCount / Rules::get().Shielded.MaxIns + 5;

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    const uint32_t pushTxCount = txCount;
    const uint32_t pullTxCount = txCount;
    Height pullTxsStartHeight = Rules::get().pForks[2].m_Height + 15;

    uint32_t nTxsPending = 0;
    auto completeAction = [&nTxsPending](auto)
    {
        assert(nTxsPending);
        --nTxsPending;
    };

    bool bTxSplit = false, bTxPush = false, bTxPull = false;

    constexpr Amount kCoinAmount = 40000000;
    constexpr Amount kFee = 20000000;
    constexpr Amount kNominalCoin = kCoinAmount + kFee;
    AmountList testAmount(txCount, kNominalCoin);

    auto senderWalletDB = createSenderWalletDB(0, 0);
    // Coin for split TX
    auto binaryTreasury = createTreasury(senderWalletDB, { (txCount + 1) * kNominalCoin });
    TestWalletRig sender(senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);

    sender.m_Wallet->RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>([=](){return senderWalletDB;}));

    sender.m_Wallet->RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());

    Node node;
    NodeObserver observer([&]()
        {
            const auto& cursor = node.get_Processor().m_Cursor;
            if (nTxsPending && cursor.m_Sid.m_Height <= 200)
                return;

            // create txCount coins(split TX)
            if (!bTxSplit)
            {
                // better not to send split tx before fork2. It can be dropped once we cross the fork
                if (cursor.m_Sid.m_Height >= Rules::get().pForks[2].m_Height+1)
                {
                    auto splitTxParameters = CreateSplitTransactionParameters(testAmount)
                        .SetParameter(TxParameterID::Fee, Amount(kNominalCoin));

                    bTxSplit = true;
                    nTxsPending = 1;

                    sender.m_Wallet->StartTransaction(splitTxParameters);
                }

                return;

            }

            // insert pushTxCount coins to shielded pool
            if (!bTxPush)
            {
                if (cursor.m_Sid.m_Height >= Rules::get().pForks[2].m_Height + 3)
                {
                    bTxPush = true;

                    for (size_t i = 0; i < pushTxCount; i++)
                    {
                        auto parameters = lelantus::CreatePushTransactionParameters()
                            .SetParameter(TxParameterID::Amount, kCoinAmount)
                            .SetParameter(TxParameterID::Fee, kFee);

                        nTxsPending++;
                        sender.m_Wallet->StartTransaction(parameters);
                    }
                }

                return;
            }

            // extract pullTxCount shielded UTXO's
            if (!bTxPull)
            {
                if (cursor.m_Sid.m_Height >= pullTxsStartHeight)
                {
                    bTxPull = true;

                    for (size_t index = 0; index < pullTxCount; index++)
                    {
                        auto parameters = lelantus::CreatePullTransactionParameters()
                            .SetParameter(TxParameterID::Amount, kCoinAmount - kFee)
                            .SetParameter(TxParameterID::Fee, kFee)
                            .SetParameter(TxParameterID::ShieldedOutputId, static_cast<TxoID>(index));

                        nTxsPending++;
                        sender.m_Wallet->StartTransaction(parameters);
                    }
                }

                return;
            }


            mainReactor->stop();
        });

    InitOwnNodeToTest(node, binaryTreasury, &observer, sender.m_WalletDB->get_MasterKdf(), 32125, 200);

    mainReactor->run();
    
    auto pullTxHistory = sender.m_WalletDB->getTxHistory(TxType::PullTransaction);
    
    WALLET_CHECK(pullTxHistory.size() == pullTxCount);
    WALLET_CHECK(std::all_of(pullTxHistory.begin(), pullTxHistory.end(), [](const auto& tx) { return tx.m_status == TxStatus::Completed; }));
}

void TestShieldedUTXORollback()
{
    cout << "\nShielded UTXO rollback test\n";
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    auto db = createSenderWalletDB();

    // create shieldedUTXO
    ShieldedCoin coin;
    Height height = 123123;
    Height spentHeight = height + 100;

    coin.m_confirmHeight = height;
    coin.m_spentHeight = spentHeight;

    db->saveShieldedCoin(coin);

    db->rollbackConfirmedShieldedUtxo(height - 100);

    auto coins = db->getShieldedCoins(Asset::s_BeamID);
    WALLET_CHECK(coins.size() == 1);
    WALLET_CHECK(coins[0].m_spentHeight == MaxHeight);
    WALLET_CHECK(coins[0].m_confirmHeight == MaxHeight);

    db->clearShieldedCoins();
    coins = db->getShieldedCoins(Asset::s_BeamID);
    WALLET_CHECK(coins.empty());
}

void TestPushTxRollbackByLowFee()
{
    cout << "\nTest rollback pushTx(reason: low fee).\n";
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    auto completeAction = [&mainReactor](auto)
    {
        mainReactor->stop();        
    };

    auto senderWalletDB = createSenderWalletDB(0, 0);
    auto binaryTreasury = createTreasury(senderWalletDB, kDefaultTestAmounts);
    TestWalletRig sender(senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);

    sender.m_Wallet->RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>([=](){return senderWalletDB;}));

    sender.m_Wallet->RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());

    Node node;
    NodeObserver observer([&]()
        {
            auto cursor = node.get_Processor().m_Cursor;
            if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
            {
                wallet::TxParameters parameters(GenerateTxID());

                parameters.SetParameter(TxParameterID::TransactionType, TxType::PushTransaction)
                    .SetParameter(TxParameterID::IsSender, true)
                    .SetParameter(TxParameterID::Amount, 38000000)
                    .SetParameter(TxParameterID::Fee, 200)
                    .SetParameter(TxParameterID::Lifetime, kDefaultTxLifetime)
                    .SetParameter(TxParameterID::PeerResponseTime, kDefaultTxResponseTime)
                    .SetParameter(TxParameterID::CreateTime, getTimestamp());

                sender.m_Wallet->StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 10)
            {
                mainReactor->stop();
            }
        });

    InitOwnNodeToTest(node, binaryTreasury, &observer, sender.m_WalletDB->get_MasterKdf(), 32125, 200);

    mainReactor->run();

    auto txHistory = sender.m_WalletDB->getTxHistory(TxType::PushTransaction);
    // check Tx params
    WALLET_CHECK(txHistory.size() == 1);
    WALLET_CHECK(txHistory[0].m_status != TxStatus::Completed);
    WALLET_CHECK(*txHistory[0].GetParameter<uint8_t>(TxParameterID::TransactionRegistered) == proto::TxStatus::LowFee);

    // currently such a tx will stuck in 'in-progress' state.
}

void TestPullTxRollbackByLowFee()
{
    cout << "\nTest rollback pullTx(reason: low fee).\n";
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    int completedCount = 2;
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
    TestWalletRig sender(senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);

    sender.m_Wallet->RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>([=](){return senderWalletDB;}));

    sender.m_Wallet->RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());

    TxID pullTxID = {};
    Node node;
    NodeObserver observer([&]()
        {
            auto cursor = node.get_Processor().m_Cursor;
            if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
            {
                auto parameters = lelantus::CreatePushTransactionParameters()
                    .SetParameter(TxParameterID::Amount, 3800000)
                    .SetParameter(TxParameterID::Fee, 1200000);

                sender.m_Wallet->StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 10)
            {
                auto parameters = lelantus::CreatePullTransactionParameters()
                    .SetParameter(TxParameterID::Amount, 3600000)
                    .SetParameter(TxParameterID::Fee, 200000)
                    .SetParameter(TxParameterID::ShieldedOutputId, 0U);

                pullTxID = sender.m_Wallet->StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 20)
            {
                mainReactor->stop();
            }
        });

    InitOwnNodeToTest(node, binaryTreasury, &observer, sender.m_WalletDB->get_MasterKdf(), 32125, 200);

    mainReactor->run();

    WALLET_CHECK(completedCount == 0);
    auto txHistory = sender.m_WalletDB->getTxHistory(TxType::PullTransaction);
    // check Tx params
    WALLET_CHECK(txHistory.size() == 1);
    WALLET_CHECK(txHistory[0].m_status == TxStatus::Failed);
    WALLET_CHECK(*txHistory[0].GetParameter<uint8_t>(TxParameterID::TransactionRegistered) == proto::TxStatus::LowFee);
    // check coins
    auto coins = sender.m_WalletDB->getCoinsByTx(pullTxID);
    WALLET_CHECK(coins.size() == 0);
    auto shieldedCoins = sender.m_WalletDB->getShieldedCoins(Asset::Asset::s_BeamID);
    WALLET_CHECK(shieldedCoins.size() == 1);
    WALLET_CHECK(!shieldedCoins[0].m_spentTxId.is_initialized());
}

void TestExpiredTxs()
{
    cout << "\nTest expired lelantus tx\n";
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
    TestWalletRig sender(senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);

    sender.m_Wallet->RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>([=](){return senderWalletDB;}));

    sender.m_Wallet->RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());

    Height startHeight = Rules::get().pForks[2].m_Height + 1;
    TxID expiredPushTxId = {};
    TxID expiredPullTxId = {};
    TxID completedPushTxId = {};
    TxID completedPullTxId = {};
    Node node;
    NodeObserver observer([&]()
        {
            auto cursor = node.get_Processor().m_Cursor;
            if (cursor.m_Sid.m_Height == startHeight)
            {
                auto parameters = lelantus::CreatePushTransactionParameters()
                    .SetParameter(TxParameterID::Amount, 38000000)
                    .SetParameter(TxParameterID::Fee, 12000000)
                    .SetParameter(TxParameterID::Lifetime, 0U);

                expiredPushTxId = sender.m_Wallet->StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == startHeight + 3)
            {
                auto parameters = lelantus::CreatePushTransactionParameters()
                    .SetParameter(TxParameterID::Amount, 78000000)
                    .SetParameter(TxParameterID::Fee, 12000000);

                completedPushTxId = sender.m_Wallet->StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == startHeight + 6)
            {
                auto parameters = lelantus::CreatePullTransactionParameters()
                    .SetParameter(TxParameterID::Amount, 66000000)
                    .SetParameter(TxParameterID::Fee, 12000000)
                    .SetParameter(TxParameterID::ShieldedOutputId, 0U)
                    .SetParameter(TxParameterID::Lifetime, 0U);

                expiredPullTxId = sender.m_Wallet->StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == startHeight + 9)
            {
                auto parameters = lelantus::CreatePullTransactionParameters()
                    .SetParameter(TxParameterID::Amount, 66000000)
                    .SetParameter(TxParameterID::Fee, 12000000)
                    .SetParameter(TxParameterID::ShieldedOutputId, 0U);

                completedPullTxId = sender.m_Wallet->StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == startHeight + 12)
            {
                mainReactor->stop();
            }
        });

    InitOwnNodeToTest(node, binaryTreasury, &observer, sender.m_WalletDB->get_MasterKdf(), 32125, 200);

    mainReactor->run();

    auto expiredPushTx = sender.m_WalletDB->getTx(expiredPushTxId);
    WALLET_CHECK(expiredPushTx->m_status == TxStatus::Failed);
    WALLET_CHECK(expiredPushTx->m_failureReason == TxFailureReason::TransactionExpired);

    auto completedPushTx = sender.m_WalletDB->getTx(completedPushTxId);
    WALLET_CHECK(completedPushTx->m_status == TxStatus::Completed);

    auto expiredPullTx = sender.m_WalletDB->getTx(expiredPullTxId);
    WALLET_CHECK(expiredPullTx->m_status == TxStatus::Failed);
    WALLET_CHECK(expiredPullTx->m_failureReason == TxFailureReason::TransactionExpired);

    auto completedPullTx = sender.m_WalletDB->getTx(completedPullTxId);
    WALLET_CHECK(completedPullTx->m_status == TxStatus::Completed);
}

void TestPushTxNoVoucherAtTime() {
    cout << "\nTest rollback pushTx (reason: no voucher at max height).\n";
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    int completedCount = 2;
    auto completeAction = [&mainReactor, &completedCount](auto)
    {
        --completedCount;
        if (completedCount == 0)
        {
            mainReactor->stop();
        }
    };

    auto senderWalletDB = createSenderWalletDB(0, 0, false, true);
    auto binaryTreasury = createTreasury(senderWalletDB, kDefaultTestAmounts);
    TestWalletRig sender(senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);
    auto receiverWalletDB = createReceiverWalletDB(false, true);
    TestWalletRig receiver(receiverWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);

    sender.m_Wallet->RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>([=](){return senderWalletDB;}));

    receiver.m_Wallet->RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());
    receiver.m_Wallet->EnableBodyRequests(true);

    auto vouchers = GenerateVoucherList(receiver.m_WalletDB->get_KeyKeeper(), receiver.m_OwnID, 2);
    Node node;
    NodeObserver observer([&]()
                          {
                              auto cursor = node.get_Processor().m_Cursor;
                              if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
                              {
                                  auto parameters = lelantus::CreatePushTransactionParameters()
                                          .SetParameter(TxParameterID::Amount, 18000000)
                                          .SetParameter(TxParameterID::Fee, 12000000)
                                          .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr)
                                          .SetParameter(TxParameterID::Voucher, vouchers.front()) // preassing the voucher
                                          .SetParameter(TxParameterID::PeerEndpoint, receiver.m_Endpoint);

                                  sender.m_Wallet->StartTransaction(parameters);
                              }
                              else if (cursor.m_Sid.m_Height == 20)
                              {
                                  // second attempt
                                  auto parameters = lelantus::CreatePushTransactionParameters()
                                          .SetParameter(TxParameterID::Amount, 18000000)
                                          .SetParameter(TxParameterID::Fee, 12000000)
                                          .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr)
                                          .SetParameter(TxParameterID::Lifetime, static_cast<Height>(1))
                                          .SetParameter(TxParameterID::Voucher, vouchers.front()) // attempt to reuse same voucher
                                          .SetParameter(TxParameterID::PeerEndpoint, receiver.m_Endpoint);

                                  sender.m_Wallet->StartTransaction(parameters);
                              }
                              else if (cursor.m_Sid.m_Height == 50)
                              {
                                  mainReactor->stop();
                              }
                          });

    InitOwnNodeToTest(node, binaryTreasury, &observer, sender.m_WalletDB->get_MasterKdf(), 32125, 200);

    mainReactor->run();

    auto txHistory = sender.m_WalletDB->getTxHistory(TxType::PushTransaction);
    // check Tx params
    WALLET_CHECK(txHistory.size() == 2);
    std::sort(txHistory.begin(), txHistory.end(), [](const auto& left, const auto& right) {return left.m_status < right.m_status; });
    WALLET_CHECK(txHistory[0].m_status == TxStatus::Completed);
    WALLET_CHECK(txHistory[1].m_status == TxStatus::Failed);

    txHistory = receiver.m_WalletDB->getTxHistory(TxType::PushTransaction);
    WALLET_CHECK(txHistory.size() == 1);
    WALLET_CHECK(txHistory[0].m_status == TxStatus::Completed);
}

void TestReextract()
{
    cout << "\nTest re-extraction of the shieldedCoin\n";
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    int completedCount = 3;
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
    TestWalletRig sender(senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);

    sender.m_Wallet->RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>([=](){return senderWalletDB;}));

    sender.m_Wallet->RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());

    TxID firstPullTxID = {};
    TxID secondPullTxID = {};

    Node node;
    NodeObserver observer([&]()
        {
            auto cursor = node.get_Processor().m_Cursor;
            if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
            {
                auto parameters = lelantus::CreatePushTransactionParameters()
                    .SetParameter(TxParameterID::Amount, 38000000)
                    .SetParameter(TxParameterID::Fee, 12000000);

                sender.m_Wallet->StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 10)
            {
                {
                    auto parameters = lelantus::CreatePullTransactionParameters()
                        .SetParameter(TxParameterID::Amount, 26000000)
                        .SetParameter(TxParameterID::Fee, 12000000)
                        .SetParameter(TxParameterID::ShieldedOutputId, 0U);
                    firstPullTxID = *parameters.GetTxID();
                    sender.m_Wallet->StartTransaction(parameters);
                }
                {
                    auto parameters = lelantus::CreatePullTransactionParameters()
                        .SetParameter(TxParameterID::Amount, 26000000)
                        .SetParameter(TxParameterID::Fee, 12000000)
                        .SetParameter(TxParameterID::ShieldedOutputId, 0U);
                    secondPullTxID = *parameters.GetTxID();
                    sender.m_Wallet->StartTransaction(parameters);
                }
            }
            else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 20)
            {
                mainReactor->stop();
            }
        });

    InitOwnNodeToTest(node, binaryTreasury, &observer, sender.m_WalletDB->get_MasterKdf(), 32125, 200);

    mainReactor->run();

    WALLET_CHECK(completedCount == 0);
    auto firstPullTx = sender.m_WalletDB->getTx(firstPullTxID);
    auto secondPullTx = sender.m_WalletDB->getTx(secondPullTxID);
    WALLET_CHECK(firstPullTx->m_status == TxStatus::Completed);
    WALLET_CHECK(secondPullTx->m_status == TxStatus::Failed);
    WALLET_CHECK(secondPullTx->m_failureReason == TxFailureReason::NoInputs);
}

int main()
{
    ECC::PseudoRandomGenerator prg;
    ECC::PseudoRandomGenerator::Scope scopePrg(&prg);
    prg.m_hv = 71U;

    int logLevel = BEAM_LOG_LEVEL_DEBUG;
    auto logger = beam::Logger::create(logLevel, logLevel);
    Rules::get().FakePoW = true;
    Rules::get().pForks[1].m_Height = 6;
    Rules::get().pForks[2].m_Height = 12;
    Rules::get().pForks[3].m_Height = 800;
    Rules::get().UpdateChecksum();

    auto testAll = []()
    {
        TestMaxPrivacyAndOffline();
        TestRestoreInterruption();
        TestTreasuryRestore();
        TestSimpleTx();
        TestMaxPrivacyTx();
        TestPublicAddressTx();
        TestDirectAnonymousPayment();
        TestPushTxNoVoucherAtTime();
        TestManyTransactons(20, Lelantus::Cfg{ 2, 5 }, Lelantus::Cfg{ 2, 3 });
        TestManyTransactons(40, Lelantus::Cfg{ 2, 5 }, Lelantus::Cfg{ 2, 3 });
        TestManyTransactons(100, Lelantus::Cfg{ 2, 5 }, Lelantus::Cfg{ 2, 3 });

        ///*TestManyTransactons();*/

        //TestShortWindow();

        TestShieldedUTXORollback();
        TestPushTxRollbackByLowFee();
        //TestPullTxRollbackByLowFee(); test won't succeed, current pull logic will automatically add inputs and/or adjust fee
        //TestExpiredTxs();

        //TestReextract();

    };
    //testAll();
    Rules::get().pForks[3].m_Height = 12;
    Rules::get().UpdateChecksum();
    testAll();
    
    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}