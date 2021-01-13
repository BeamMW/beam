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
    sender.m_Wallet.RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>(senderWalletDB));
    receiver.m_Wallet.RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>(receiverWalletDB));

    sender.m_Wallet.Rescan();
    Node node;
    NodeObserver observer([&]()
    {
        auto cursor = node.get_Processor().m_Cursor;
        if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
        {
            auto walletAddress = GenerateNewAddress(receiver.m_WalletDB, "", WalletAddress::ExpirationStatus::Never);
            auto vouchers = GenerateVoucherList(receiver.m_WalletDB->get_KeyKeeper(), walletAddress.m_OwnID, 1);
            auto newAddress = GenerateOfflineAddress(walletAddress, 0, vouchers);
            auto p = ParseParameters(newAddress);
            WALLET_CHECK(p);
            auto parameters = lelantus::CreatePushTransactionParameters(sender.m_WalletID)
                .SetParameter(TxParameterID::Amount, 38000000)
                .SetParameter(TxParameterID::Fee, 12000000);

            LoadReceiverParams(*p, parameters);

            sender.m_Wallet.StartTransaction(parameters);
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
        WALLET_CHECK(shieldedCoins[0].m_Status == ShieldedCoin::Status::Available);
    }
}

void TestUnlinkTx()
{
    cout << "\nTesting unlink funds transaction (push + pull)...\n";
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    // save defaults
    ScopedGlobalRules rules;
    Rules::get().Shielded.m_ProofMax = { 2, 6 }; // 64
    Rules::get().Shielded.m_ProofMin = { 2, 4 }; // 16

    const int PushTxCount = 64;
    int completedCount = PushTxCount + 1;
    auto completeAction = [&mainReactor, &completedCount](auto)
    {
        --completedCount;
        if (completedCount == 0)
        {
            mainReactor->stop();
        }
    };

    auto senderWalletDB = createSenderWalletDB(0, 0);
    std::vector<Amount> amounts(PushTxCount, Amount(8000000000));
    amounts.push_back(Amount(50000000));

    auto binaryTreasury = createTreasury(senderWalletDB, amounts);
    TestWalletRig sender(senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);

    sender.m_Wallet.RegisterTransactionType(TxType::UnlinkFunds, std::make_shared<lelantus::UnlinkFundsTransaction::Creator>());

    sender.m_Wallet.RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>(senderWalletDB));

    Node node;
    NodeObserver observer([&]()
        {
            auto cursor = node.get_Processor().m_Cursor;
            if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
            {
                auto parameters = lelantus::CreateUnlinkFundsTransactionParameters(sender.m_WalletID)
                    .SetParameter(TxParameterID::Amount, Amount(38000000))
                    .SetParameter(TxParameterID::Fee, Amount(12000000));

                sender.m_Wallet.StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == 20)
            {
                // fill the pool
                for (int i = 0; i < PushTxCount; ++i)
                {
                    auto parameters = lelantus::CreatePushTransactionParameters(sender.m_WalletID)
                        .SetParameter(TxParameterID::Amount, Amount(18000000))
                        .SetParameter(TxParameterID::Fee, Amount(12000000));

                    sender.m_Wallet.StartTransaction(parameters);
                }
            }
            else if (cursor.m_Sid.m_Height == 70)
            {
                mainReactor->stop();
            }
        });

    InitOwnNodeToTest(node, binaryTreasury, &observer, sender.m_WalletDB->get_MasterKdf(), 32125, 200);

    mainReactor->run();

    WALLET_CHECK(completedCount == 0);
    auto txHistory = sender.m_WalletDB->getTxHistory(TxType::ALL);
    auto it = std::find_if(txHistory.begin(), txHistory.end(), [](const auto& tx) {return  tx.m_txType == TxType::UnlinkFunds; });
    WALLET_CHECK(it != txHistory.end());
    const auto& tx = *it;
    WALLET_CHECK(tx.m_txType == TxType::UnlinkFunds);
    WALLET_CHECK(tx.m_status == TxStatus::Completed);
    auto coins = sender.m_WalletDB->getCoinsCreatedByTx(tx.m_txId);
    WALLET_CHECK(coins.size() == 1);
    auto& coin = coins[0];
    WALLET_CHECK(coin.m_ID.m_Value == 26000000);
}

void TestCancelUnlinkTx()
{
    cout << "\nTesting unlink funds transaction cancellation...\n";
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    // save defaults
    ScopedGlobalRules rules;
    Rules::get().Shielded.m_ProofMax = { 2, 6 }; // 64
    Rules::get().Shielded.m_ProofMin = { 2, 4 }; // 16

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

    sender.m_Wallet.RegisterTransactionType(TxType::UnlinkFunds, std::make_shared<lelantus::UnlinkFundsTransaction::Creator>());

    sender.m_Wallet.RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>(senderWalletDB));
    TxID txID = {0};
    Node node;
    NodeObserver observer([&]()
        {
            auto cursor = node.get_Processor().m_Cursor;
            if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
            {
                auto parameters = lelantus::CreateUnlinkFundsTransactionParameters(sender.m_WalletID)
                    .SetParameter(TxParameterID::Amount, Amount(38000000))
                    .SetParameter(TxParameterID::Fee, Amount(12000000));

                txID = sender.m_Wallet.StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 7)
            {
                sender.m_Wallet.CancelTransaction(txID);
            }
            else if (cursor.m_Sid.m_Height == 70)
            {
                mainReactor->stop();
            }
        });

    InitOwnNodeToTest(node, binaryTreasury, &observer, sender.m_WalletDB->get_MasterKdf(), 32125, 200);

    mainReactor->run();

    WALLET_CHECK(completedCount == 0);
    auto txHistory = sender.m_WalletDB->getTxHistory(TxType::ALL);
    auto it = std::find_if(txHistory.begin(), txHistory.end(), [](const auto& tx) {return  tx.m_txType == TxType::UnlinkFunds; });
    WALLET_CHECK(it != txHistory.end());
    const auto& tx = *it;
    WALLET_CHECK(tx.m_txType == TxType::UnlinkFunds);
    WALLET_CHECK(tx.m_status == TxStatus::Completed);
    auto coins = sender.m_WalletDB->getCoinsCreatedByTx(tx.m_txId);
    WALLET_CHECK(coins.size() == 1);
    auto& coin = coins[0];
    WALLET_CHECK(coin.m_ID.m_Value == 26000000);
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

    sender.m_Wallet.RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>(senderWalletDB));

    sender.m_Wallet.RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());

    Node node;
    NodeObserver observer([&]()
    {
        auto cursor = node.get_Processor().m_Cursor;
        if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
        {
            auto parameters = lelantus::CreatePushTransactionParameters(sender.m_WalletID)
                .SetParameter(TxParameterID::Amount, 38000000)
                .SetParameter(TxParameterID::Fee, 12000000); 

            sender.m_Wallet.StartTransaction(parameters);
        }
        else if (cursor.m_Sid.m_Height == 30)
        {
            auto parameters = lelantus::CreatePushTransactionParameters(sender.m_WalletID)
                .SetParameter(TxParameterID::Amount, 78000000)
                .SetParameter(TxParameterID::Fee, 12000000);

            sender.m_Wallet.StartTransaction(parameters);
        }
        //else if (cursor.m_Sid.m_Height == 40)
        //{
        //    auto parameters = lelantus::CreatePullTransactionParameters(sender.m_WalletID)
        //        .SetParameter(TxParameterID::Amount, 66000000)
        //        .SetParameter(TxParameterID::AmountList, AmountList{ 46000000, 20000000 })
        //        .SetParameter(TxParameterID::Fee, 12000000)
        //        .SetParameter(TxParameterID::ShieldedOutputId, 1U);
        //
        //    sender.m_Wallet.StartTransaction(parameters);
        //}
        //else if (cursor.m_Sid.m_Height == 50)
        //{
        //    auto parameters = lelantus::CreatePullTransactionParameters(sender.m_WalletID)
        //        .SetParameter(TxParameterID::Amount, 26000000)
        //        .SetParameter(TxParameterID::Fee, 12000000)
        //        .SetParameter(TxParameterID::ShieldedOutputId, 0U);
        //
        //    sender.m_Wallet.StartTransaction(parameters);
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

    sender.m_Wallet.RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>(senderWalletDB));

    receiver.m_Wallet.RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());

    // generate ticket from public address
    Scalar::Native sk;
    sk.GenRandomNnz();
    PeerID pid;  // fake peedID
    pid.FromSk(sk);

    ShieldedTxo::PublicGen gen = GeneratePublicAddress(*receiver.m_WalletDB->get_OwnerKdf(), 0);

    ByteBuffer buf = toByteBuffer(gen);

    ShieldedTxo::PublicGen gen2;
    WALLET_CHECK(fromByteBuffer(buf, gen2));

    ShieldedTxo::Voucher voucher = GenerateVoucherFromPublicAddress(gen2, sk);

    TxID txID = {};
    Node node;
    NodeObserver observer([&]()
    {
        auto cursor = node.get_Processor().m_Cursor;
        if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
        {
            auto parameters = lelantus::CreatePushTransactionParameters(sender.m_WalletID)
                .SetParameter(TxParameterID::Amount, 18000000)
                .SetParameter(TxParameterID::Fee, 12000000)
                .SetParameter(TxParameterID::PeerID, receiver.m_WalletID)
                .SetParameter(TxParameterID::Voucher, voucher) // preassing the voucher
                .SetParameter(TxParameterID::PeerWalletIdentity, pid)
                .SetParameter(TxParameterID::MaxPrivacyMinAnonimitySet, uint8_t(64));

            txID = sender.m_Wallet.StartTransaction(parameters);
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

    sender.m_Wallet.RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>(senderWalletDB));

    receiver.m_Wallet.RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());

    // generate ticket from public address
    Scalar::Native sk;
    sk.GenRandomNnz();
    PeerID pid;  // fake peedID
    pid.FromSk(sk);

    ShieldedTxo::PublicGen gen = GeneratePublicAddress(*receiver.m_WalletDB->get_OwnerKdf(), 0);

    ByteBuffer buf = toByteBuffer(gen);

    ShieldedTxo::PublicGen gen2;
    WALLET_CHECK(fromByteBuffer(buf, gen2));

    ShieldedTxo::Voucher voucher = GenerateVoucherFromPublicAddress(gen2, sk);

    TxID txID = {};
    Node node;
    NodeObserver observer([&]()
    {
        auto cursor = node.get_Processor().m_Cursor;
        if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
        {
            auto parameters = lelantus::CreatePushTransactionParameters(sender.m_WalletID)
                .SetParameter(TxParameterID::Amount, 18000000)
                .SetParameter(TxParameterID::Fee, 12000000)
                .SetParameter(TxParameterID::PeerID, receiver.m_WalletID)
                .SetParameter(TxParameterID::Voucher, voucher) // preassing the voucher
                .SetParameter(TxParameterID::PeerWalletIdentity, pid);

            txID = sender.m_Wallet.StartTransaction(parameters);
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

    sender.m_Wallet.RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>(senderWalletDB));

    receiver.m_Wallet.RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());
    receiver.m_Wallet.EnableBodyRequests(true);

    auto vouchers = GenerateVoucherList(receiver.m_WalletDB->get_KeyKeeper(), receiver.m_OwnID, 2);
    WALLET_CHECK(IsValidVoucherList(vouchers, receiver.m_SecureWalletID));
    WALLET_CHECK(!IsValidVoucherList(vouchers, sender.m_SecureWalletID));

    Node node;
    NodeObserver observer([&]()
        {
            auto cursor = node.get_Processor().m_Cursor;
            if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
            {
                auto parameters = lelantus::CreatePushTransactionParameters(sender.m_WalletID)
                    .SetParameter(TxParameterID::Amount, 18000000)
                    .SetParameter(TxParameterID::Fee, 12000000)
                    .SetParameter(TxParameterID::PeerID, receiver.m_WalletID)
                    .SetParameter(TxParameterID::Voucher, vouchers.front()) // preassing the voucher
                    .SetParameter(TxParameterID::PeerWalletIdentity, receiver.m_SecureWalletID)
                    .SetParameter(TxParameterID::PeerOwnID, receiver.m_OwnID);

                sender.m_Wallet.StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == 20)
            {
                // second attempt
                auto parameters = lelantus::CreatePushTransactionParameters(sender.m_WalletID)
                    .SetParameter(TxParameterID::Amount, 18000000)
                    .SetParameter(TxParameterID::Fee, 12000000)
                    .SetParameter(TxParameterID::PeerID, receiver.m_WalletID)
                    .SetParameter(TxParameterID::Voucher, vouchers.front()) // attempt to reuse same voucher
                    .SetParameter(TxParameterID::PeerWalletIdentity, receiver.m_SecureWalletID)
                    .SetParameter(TxParameterID::PeerOwnID, receiver.m_OwnID);
            
                sender.m_Wallet.StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == 26)
            {
                auto parameters = lelantus::CreatePushTransactionParameters(sender.m_WalletID)
                    .SetParameter(TxParameterID::Amount, 18000000)
                    .SetParameter(TxParameterID::Fee, 12000000)
                    .SetParameter(TxParameterID::PeerID, receiver.m_WalletID)
                    .SetParameter(TxParameterID::PeerWalletIdentity, receiver.m_SecureWalletID)
                    .SetParameter(TxParameterID::PeerOwnID, receiver.m_OwnID);
            
                sender.m_Wallet.StartTransaction(parameters);
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

            WALLET_CHECK(txHistory[0].m_myId == receiver.m_WalletID);
            WALLET_CHECK(txHistory[0].getReceiverIdentity() == std::to_string(receiver.m_SecureWalletID));
            ByteBuffer b = storage::ExportPaymentProof(*sender.m_WalletDB, txID);

            WALLET_CHECK(storage::VerifyPaymentProof(b));

            auto pi2 = storage::ShieldedPaymentInfo::FromByteBuffer(b);

            TxKernel::Ptr k;
            ShieldedTxo::Voucher voucher;
            Amount amount = 0;
            Asset::ID assetID = Asset::s_InvalidID;;
            PeerID peerIdentity = Zero;
            PeerID myIdentity = Zero;
            bool success = true;
            success &= storage::getTxParameter(*sender.m_WalletDB, txID, TxParameterID::Kernel, k);
            WALLET_CHECK(k->get_Subtype() == TxKernel::Subtype::Std);

            auto& kernel = k->m_vNested[0]->CastTo_ShieldedOutput();
            success &= storage::getTxParameter(*sender.m_WalletDB, txID, TxParameterID::Voucher, voucher);
            success &= storage::getTxParameter(*sender.m_WalletDB, txID, TxParameterID::PeerWalletIdentity, peerIdentity);
            success &= storage::getTxParameter(*sender.m_WalletDB, txID, TxParameterID::MyWalletIdentity, myIdentity);
            success &= storage::getTxParameter(*sender.m_WalletDB, txID, TxParameterID::Amount, amount);
            storage::getTxParameter(*sender.m_WalletDB, txID, TxParameterID::AssetID, assetID);

            WALLET_CHECK(pi2.m_KernelID == k->m_Internal.m_ID);

            WALLET_CHECK(success);
            {
                ShieldedTxo::Voucher voucher2;
                voucher2.m_SharedSecret = voucher.m_SharedSecret;
                voucher2.m_Signature = voucher.m_Signature;
                voucher2.m_Ticket = kernel.m_Txo.m_Ticket;

                WALLET_CHECK(voucher2.IsValid(peerIdentity));

                ECC::Oracle oracle;
                oracle << kernel.m_Msg;
                ShieldedTxo::Data::OutputParams outputParams;
                WALLET_CHECK(outputParams.Recover(kernel.m_Txo, voucher.m_SharedSecret, oracle));
                WALLET_CHECK(outputParams.m_Value == amount);
                WALLET_CHECK(outputParams.m_AssetID == assetID);
                WALLET_CHECK(outputParams.m_User.m_Sender == myIdentity);
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

    sender.m_Wallet.RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>(senderWalletDB));

    sender.m_Wallet.RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());

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
                    .SetParameter(TxParameterID::Amount, 38000000)
                    .SetParameter(TxParameterID::Fee, 12000000)
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
                    .SetParameter(TxParameterID::Amount, 38000000)
                    .SetParameter(TxParameterID::Fee, 12000000)
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
                    .SetParameter(TxParameterID::Amount, 38000000)
                    .SetParameter(TxParameterID::Fee, 12000000)
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


    sender.m_Wallet.RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>(senderWalletDB));

    sender.m_Wallet.RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());

    Node node;
    NodeObserver observer([&]()
        {
            auto cursor = node.get_Processor().m_Cursor;
            // create 300(kCount) coins(split TX)
            if (cursor.m_Sid.m_Height == 3)
            {
                auto splitTxParameters = CreateSplitTransactionParameters(sender.m_WalletID, testAmount)
                    .SetParameter(TxParameterID::Fee, Amount(kNominalCoin));

                sender.m_Wallet.StartTransaction(splitTxParameters);
            }
            // insert 300(kCount) coins to shielded pool
            else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
            {
                for (size_t i = 0; i < kCount; i++)
                {
                    auto parameters = lelantus::CreatePushTransactionParameters(sender.m_WalletID)
                        .SetParameter(TxParameterID::Amount, kCoinAmount)
                        .SetParameter(TxParameterID::Fee, kFee);

                    sender.m_Wallet.StartTransaction(parameters);
                }
            }
            // extract one of first shielded UTXO
            else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 15)
            {
                {
                    auto parameters = lelantus::CreatePullTransactionParameters(sender.m_WalletID)
                        .SetParameter(TxParameterID::Amount, kCoinAmount - kFee)
                        .SetParameter(TxParameterID::Fee, kFee)
                        .SetParameter(TxParameterID::ShieldedOutputId, 40U);

                    sender.m_Wallet.StartTransaction(parameters);
                }
                {
                    auto parameters = lelantus::CreatePullTransactionParameters(sender.m_WalletID)
                        .SetParameter(TxParameterID::Amount, kCoinAmount - kFee)
                        .SetParameter(TxParameterID::Fee, kFee)
                        .SetParameter(TxParameterID::ShieldedOutputId, 42U);

                    sender.m_Wallet.StartTransaction(parameters);
                }
                {
                    auto parameters = lelantus::CreatePullTransactionParameters(sender.m_WalletID)
                        .SetParameter(TxParameterID::Amount, kCoinAmount - kFee)
                        .SetParameter(TxParameterID::Fee, kFee)
                        .SetParameter(TxParameterID::ShieldedOutputId, 43U);

                    sender.m_Wallet.StartTransaction(parameters);
                }
            }
            else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 20)
            {
                auto parameters = lelantus::CreatePullTransactionParameters(sender.m_WalletID)
                    .SetParameter(TxParameterID::Amount, kCoinAmount - kFee)
                    .SetParameter(TxParameterID::Fee, kFee)
                    .SetParameter(TxParameterID::ShieldedOutputId, 62U);

                sender.m_Wallet.StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 30)
            {
                auto parameters = lelantus::CreatePullTransactionParameters(sender.m_WalletID)
                    .SetParameter(TxParameterID::Amount, kCoinAmount - kFee)
                    .SetParameter(TxParameterID::Fee, kFee)
                    .SetParameter(TxParameterID::ShieldedOutputId, 180);

                sender.m_Wallet.StartTransaction(parameters);
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

    sender.m_Wallet.RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>(senderWalletDB));

    sender.m_Wallet.RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());

    Node node;
    NodeObserver observer([&]()
        {
            if (nTxsPending)
                return;
            const auto& cursor = node.get_Processor().m_Cursor;

            // create txCount coins(split TX)
            if (!bTxSplit)
            {
                // better not to send split tx before fork2. It can be dropped once we cross the fork
                if (cursor.m_Sid.m_Height >= Rules::get().pForks[2].m_Height)
                {
                    auto splitTxParameters = CreateSplitTransactionParameters(sender.m_WalletID, testAmount)
                        .SetParameter(TxParameterID::Fee, Amount(kNominalCoin));

                    bTxSplit = true;
                    nTxsPending = 1;

                    sender.m_Wallet.StartTransaction(splitTxParameters);
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
                        auto parameters = lelantus::CreatePushTransactionParameters(sender.m_WalletID)
                            .SetParameter(TxParameterID::Amount, kCoinAmount)
                            .SetParameter(TxParameterID::Fee, kFee);

                        nTxsPending++;
                        sender.m_Wallet.StartTransaction(parameters);
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
                        auto parameters = lelantus::CreatePullTransactionParameters(sender.m_WalletID)
                            .SetParameter(TxParameterID::Amount, kCoinAmount - kFee)
                            .SetParameter(TxParameterID::Fee, kFee)
                            .SetParameter(TxParameterID::ShieldedOutputId, static_cast<TxoID>(index));

                        nTxsPending++;
                        sender.m_Wallet.StartTransaction(parameters);
                    }
                }

                return;
            }

            mainReactor->stop();
        });

    InitOwnNodeToTest(node, binaryTreasury, &observer, sender.m_WalletDB->get_MasterKdf(), 32125, 200);

    mainReactor->run();
    
    // TODO: commented PullTransaction is outdated doesn't work
    //auto pullTxHistory = sender.m_WalletDB->getTxHistory(TxType::PullTransaction);
    //
    //WALLET_CHECK(pullTxHistory.size() == pullTxCount);
    //WALLET_CHECK(std::all_of(pullTxHistory.begin(), pullTxHistory.end(), [](const auto& tx) { return tx.m_status == TxStatus::Completed; }));
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

    sender.m_Wallet.RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>(senderWalletDB));

    sender.m_Wallet.RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());

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
                    .SetParameter(TxParameterID::MyID, sender.m_WalletID)
                    .SetParameter(TxParameterID::Lifetime, kDefaultTxLifetime)
                    .SetParameter(TxParameterID::PeerResponseTime, kDefaultTxResponseTime)
                    .SetParameter(TxParameterID::CreateTime, getTimestamp());

                sender.m_Wallet.StartTransaction(parameters);
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

    sender.m_Wallet.RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>(senderWalletDB));

    sender.m_Wallet.RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());

    TxID pullTxID = {};
    Node node;
    NodeObserver observer([&]()
        {
            auto cursor = node.get_Processor().m_Cursor;
            if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
            {
                auto parameters = lelantus::CreatePushTransactionParameters(sender.m_WalletID)
                    .SetParameter(TxParameterID::Amount, 3800000)
                    .SetParameter(TxParameterID::Fee, 1200000);

                sender.m_Wallet.StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 10)
            {
                auto parameters = lelantus::CreatePullTransactionParameters(sender.m_WalletID)
                    .SetParameter(TxParameterID::Amount, 3600000)
                    .SetParameter(TxParameterID::Fee, 200000)
                    .SetParameter(TxParameterID::ShieldedOutputId, 0U);

                pullTxID = sender.m_Wallet.StartTransaction(parameters);
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

    sender.m_Wallet.RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>(senderWalletDB));

    sender.m_Wallet.RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());

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
                auto parameters = lelantus::CreatePushTransactionParameters(sender.m_WalletID)
                    .SetParameter(TxParameterID::Amount, 38000000)
                    .SetParameter(TxParameterID::Fee, 12000000)
                    .SetParameter(TxParameterID::Lifetime, 0U);

                expiredPushTxId = sender.m_Wallet.StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == startHeight + 3)
            {
                auto parameters = lelantus::CreatePushTransactionParameters(sender.m_WalletID)
                    .SetParameter(TxParameterID::Amount, 78000000)
                    .SetParameter(TxParameterID::Fee, 12000000);

                completedPushTxId = sender.m_Wallet.StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == startHeight + 6)
            {
                auto parameters = lelantus::CreatePullTransactionParameters(sender.m_WalletID)
                    .SetParameter(TxParameterID::Amount, 66000000)
                    .SetParameter(TxParameterID::Fee, 12000000)
                    .SetParameter(TxParameterID::ShieldedOutputId, 0U)
                    .SetParameter(TxParameterID::Lifetime, 0U);

                expiredPullTxId = sender.m_Wallet.StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == startHeight + 9)
            {
                auto parameters = lelantus::CreatePullTransactionParameters(sender.m_WalletID)
                    .SetParameter(TxParameterID::Amount, 66000000)
                    .SetParameter(TxParameterID::Fee, 12000000)
                    .SetParameter(TxParameterID::ShieldedOutputId, 0U);

                completedPullTxId = sender.m_Wallet.StartTransaction(parameters);
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

    sender.m_Wallet.RegisterTransactionType(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>(senderWalletDB));

    sender.m_Wallet.RegisterTransactionType(TxType::PullTransaction, std::make_shared<lelantus::PullTransaction::Creator>());

    TxID firstPullTxID = {};
    TxID secondPullTxID = {};

    Node node;
    NodeObserver observer([&]()
        {
            auto cursor = node.get_Processor().m_Cursor;
            if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
            {
                auto parameters = lelantus::CreatePushTransactionParameters(sender.m_WalletID)
                    .SetParameter(TxParameterID::Amount, 38000000)
                    .SetParameter(TxParameterID::Fee, 12000000);

                sender.m_Wallet.StartTransaction(parameters);
            }
            else if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 10)
            {
                {
                    auto parameters = lelantus::CreatePullTransactionParameters(sender.m_WalletID)
                        .SetParameter(TxParameterID::Amount, 26000000)
                        .SetParameter(TxParameterID::Fee, 12000000)
                        .SetParameter(TxParameterID::ShieldedOutputId, 0U);
                    firstPullTxID = *parameters.GetTxID();
                    sender.m_Wallet.StartTransaction(parameters);
                }
                {
                    auto parameters = lelantus::CreatePullTransactionParameters(sender.m_WalletID)
                        .SetParameter(TxParameterID::Amount, 26000000)
                        .SetParameter(TxParameterID::Fee, 12000000)
                        .SetParameter(TxParameterID::ShieldedOutputId, 0U);
                    secondPullTxID = *parameters.GetTxID();
                    sender.m_Wallet.StartTransaction(parameters);
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
    int logLevel = LOG_LEVEL_WARNING;
    auto logger = beam::Logger::create(logLevel, logLevel);
    Rules::get().FakePoW = true;
    Rules::get().UpdateChecksum();
    Height fork1Height = 6;
    Height fork2Height = 12;
    Rules::get().pForks[1].m_Height = fork1Height;
    Rules::get().pForks[2].m_Height = fork2Height;


    //TestUnlinkTx();
    //TestCancelUnlinkTx();
    TestTreasuryRestore();
    TestSimpleTx();
    TestMaxPrivacyTx();
    TestPublicAddressTx();
    TestDirectAnonymousPayment();
    TestManyTransactons(20, Lelantus::Cfg{2, 5}, Lelantus::Cfg{2, 3});
    TestManyTransactons(40, Lelantus::Cfg{ 2, 5 }, Lelantus::Cfg{ 2, 3 });
    TestManyTransactons(100, Lelantus::Cfg{ 2, 5 }, Lelantus::Cfg{ 2, 3 });

    ///*TestManyTransactons();*/

    //TestShortWindow();

    TestShieldedUTXORollback();
    TestPushTxRollbackByLowFee();
    //TestPullTxRollbackByLowFee(); test won't succeed, current pull logic will automatically add inputs and/or adjust fee
    //TestExpiredTxs();

    //TestReextract();

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}