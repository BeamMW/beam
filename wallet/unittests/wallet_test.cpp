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

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif

#include "wallet/common.h"
#include "wallet/wallet_network.h"
#include "wallet/wallet.h"
#include "wallet/secstring.h"
#include "wallet/base58.h"
#include "utility/test_helpers.h"
#include "core/radixtree.h"
#include "core/unittest/mini_blockchain.h"
#include "wallet/wallet_transaction.h"
#include "core/negotiator.h"
#include "node/node.h"
#include "keykeeper/local_private_key_keeper.h"
#include "keykeeper/trezor_key_keeper.h"

#include "test_helpers.h"

#include <string_view>
#include <assert.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

#include "core/proto.h"
#include <boost/filesystem.hpp>
#include <boost/intrusive/list.hpp>

#if defined(BEAM_HW_WALLET)
#include "keykeeper/hw_wallet.h"
#endif

using namespace beam;
using namespace std;
using namespace ECC;

WALLET_TEST_INIT

#include "wallet_test_environment.cpp"

namespace
{
    void TestWalletNegotiation(IWalletDB::Ptr senderWalletDB, IWalletDB::Ptr receiverWalletDB)
    {
        cout << "\nTesting wallets negotiation...\n";

        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);
        auto receiverKeyKeeper = std::make_shared<LocalPrivateKeyKeeper>(receiverWalletDB, receiverWalletDB->get_MasterKdf());

        WalletAddress wa = storage::createAddress(*receiverWalletDB, receiverKeyKeeper);
        receiverWalletDB->saveAddress(wa);
        WalletID receiver_id = wa.m_walletID;

        auto senderKeyKeeper = std::make_shared<LocalPrivateKeyKeeper>(senderWalletDB, senderWalletDB->get_MasterKdf());
        wa = storage::createAddress(*senderWalletDB, senderKeyKeeper);
        senderWalletDB->saveAddress(wa);
        WalletID sender_id = wa.m_walletID;

        int count = 0;
        auto f = [&count](const auto& /*id*/)
        {
            if (++count >= 2)
                io::Reactor::get_Current().stop();
        };

        TestNodeNetwork::Shared tnns;

        Wallet sender(senderWalletDB, senderKeyKeeper, f);
        Wallet receiver(receiverWalletDB, receiverKeyKeeper, f);

        auto twn = make_shared<TestWalletNetwork>();
        auto netNodeS = make_shared<TestNodeNetwork>(tnns, sender);
        auto netNodeR = make_shared<TestNodeNetwork>(tnns, receiver);

        sender.AddMessageEndpoint(twn);
        sender.SetNodeEndpoint(netNodeS);

        receiver.AddMessageEndpoint(twn);
        receiver.SetNodeEndpoint(netNodeR);

        twn->m_Map[sender_id].m_pSink = &sender;
        twn->m_Map[receiver_id].m_pSink = &receiver;

        tnns.AddBlock();

        sender.StartTransaction(CreateSimpleTransactionParameters()
            .SetParameter(TxParameterID::MyID, sender_id)
            .SetParameter(TxParameterID::PeerID, receiver_id)
            .SetParameter(TxParameterID::Amount, Amount(6))
            .SetParameter(TxParameterID::Fee, Amount(1))
            .SetParameter(TxParameterID::Lifetime, Height(200)));
        mainReactor->run();

        WALLET_CHECK(count == 2);
    }

    void TestTxToHimself()
    {
        cout << "\nTesting Tx to himself...\n";

        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);

        auto senderWalletDB = createSqliteWalletDB("sender_wallet.db", false);

        // add coin with keyType - Coinbase
        beam::Amount coin_amount = 40;
        Coin coin = CreateAvailCoin(coin_amount, 0);
        coin.m_ID.m_Type = Key::Type::Coinbase;
        senderWalletDB->storeCoin(coin);

        auto coins = senderWalletDB->selectCoins(24);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_ID.m_Type == Key::Type::Coinbase);
        WALLET_CHECK(coins[0].m_status == Coin::Available);
        WALLET_CHECK(senderWalletDB->getTxHistory().empty());

        TestNode node;
        TestWalletRig sender("sender", senderWalletDB, [](auto) { io::Reactor::get_Current().stop(); });
        helpers::StopWatch sw;

        sw.start();

        auto txId = sender.m_Wallet.StartTransaction(CreateSimpleTransactionParameters()
                    .SetParameter(TxParameterID::MyID, sender.m_WalletID)
                    .SetParameter(TxParameterID::PeerID, sender.m_WalletID)
                    .SetParameter(TxParameterID::Amount, Amount(24))
                    .SetParameter(TxParameterID::Fee, Amount(2))
                    .SetParameter(TxParameterID::Lifetime, Height(200)));

        mainReactor->run();
        sw.stop();

        cout << "Transfer elapsed time: " << sw.milliseconds() << " ms\n";

        // check Tx
        auto txHistory = senderWalletDB->getTxHistory();
        WALLET_CHECK(txHistory.size() == 1);
        WALLET_CHECK(txHistory[0].m_txId == txId);
        WALLET_CHECK(txHistory[0].m_amount == 24);
        WALLET_CHECK(txHistory[0].m_change == 14);
        WALLET_CHECK(txHistory[0].m_fee == 2);
        WALLET_CHECK(txHistory[0].m_status == TxStatus::Completed);

        // check coins
        vector<Coin> newSenderCoins;
        senderWalletDB->visitCoins([&newSenderCoins](const Coin& c)->bool
        {
            newSenderCoins.push_back(c);
            return true;
        });

        WALLET_CHECK(newSenderCoins.size() == 3);

        WALLET_CHECK(newSenderCoins[0].m_ID.m_Type == Key::Type::Coinbase);
        WALLET_CHECK(newSenderCoins[0].m_status == Coin::Spent);
        WALLET_CHECK(newSenderCoins[0].m_ID.m_Value == 40);

        WALLET_CHECK(newSenderCoins[1].m_ID.m_Type == Key::Type::Change);
        WALLET_CHECK(newSenderCoins[1].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[1].m_ID.m_Value == 14);

        WALLET_CHECK(newSenderCoins[2].m_ID.m_Type == Key::Type::Regular);
        WALLET_CHECK(newSenderCoins[2].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[2].m_ID.m_Value == 24);

        cout << "\nFinish of testing Tx to himself...\n";
    }

    void TestP2PWalletNegotiationST()
    {
        cout << "\nTesting p2p wallets negotiation single thread...\n";

        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);

        int completedCount = 2;
        auto f = [&completedCount, mainReactor](auto)
        {
            --completedCount;
            if (completedCount == 0)
            {
                mainReactor->stop();
                completedCount = 2;
            }
        };

        TestNode node;
        TestWalletRig sender("sender", createSenderWalletDB(), f, TestWalletRig::Type::Regular, false, 0);
        TestWalletRig receiver("receiver", createReceiverWalletDB(), f);

        WALLET_CHECK(sender.m_WalletDB->selectCoins(6).size() == 2);
        WALLET_CHECK(sender.m_WalletDB->getTxHistory().empty());
        WALLET_CHECK(receiver.m_WalletDB->getTxHistory().empty());

        helpers::StopWatch sw;
        sw.start();

        auto txId = sender.m_Wallet.StartTransaction(CreateSimpleTransactionParameters()
            .SetParameter(TxParameterID::MyID, sender.m_WalletID)
            .SetParameter(TxParameterID::PeerID, receiver.m_WalletID)
            .SetParameter(TxParameterID::Amount, Amount(4))
            .SetParameter(TxParameterID::Fee, Amount(2))
            .SetParameter(TxParameterID::Lifetime, Height(200))
            .SetParameter(TxParameterID::PeerResponseTime, Height(20)));

        mainReactor->run();
        sw.stop();
        cout << "First transfer elapsed time: " << sw.milliseconds() << " ms\n";

        // check coins
        vector<Coin> newSenderCoins = sender.GetCoins();
        vector<Coin> newReceiverCoins = receiver.GetCoins();

        WALLET_CHECK(newSenderCoins.size() == 4);
        WALLET_CHECK(newReceiverCoins.size() == 1);
        WALLET_CHECK(newReceiverCoins[0].m_ID.m_Value == 4);
        WALLET_CHECK(newReceiverCoins[0].m_status == Coin::Available);
        WALLET_CHECK(newReceiverCoins[0].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newSenderCoins[0].m_ID.m_Value == 5);
        WALLET_CHECK(newSenderCoins[0].m_status == Coin::Spent);
        WALLET_CHECK(newSenderCoins[0].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newSenderCoins[1].m_ID.m_Value == 2);
        WALLET_CHECK(newSenderCoins[1].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[1].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newSenderCoins[2].m_ID.m_Value == 1);
        WALLET_CHECK(newSenderCoins[2].m_status == Coin::Spent);
        WALLET_CHECK(newSenderCoins[2].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newSenderCoins[3].m_ID.m_Value == 9);
        WALLET_CHECK(newSenderCoins[3].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[3].m_ID.m_Type == Key::Type::Regular);

        // Tx history check
        auto sh = sender.m_WalletDB->getTxHistory();
        WALLET_CHECK(sh.size() == 1);
        auto rh = receiver.m_WalletDB->getTxHistory();
        WALLET_CHECK(rh.size() == 1);
        auto stx = sender.m_WalletDB->getTx(txId);
        WALLET_CHECK(stx.is_initialized());
        auto rtx = receiver.m_WalletDB->getTx(txId);
        WALLET_CHECK(rtx.is_initialized());

        WALLET_CHECK(stx->m_txId == rtx->m_txId);
        WALLET_CHECK(stx->m_amount == rtx->m_amount);
        WALLET_CHECK(stx->m_status == TxStatus::Completed);
        WALLET_CHECK(stx->m_fee == rtx->m_fee);
        WALLET_CHECK(stx->m_message == rtx->m_message);
        WALLET_CHECK(stx->m_createTime <= rtx->m_createTime);
        WALLET_CHECK(stx->m_status == rtx->m_status);
        WALLET_CHECK(stx->m_sender == true);
        WALLET_CHECK(rtx->m_sender == false);

        // rollback test
        {

            Block::SystemState::Full sTip;
            receiver.m_WalletDB->get_History().get_Tip(sTip);

            receiver.m_WalletDB->get_History().DeleteFrom(sTip.m_Height); // delete latest block

            proto::FlyClient& flyClient = receiver.m_Wallet;
            //imitate rollback
            flyClient.OnRolledBack();
            receiver.m_WalletDB->get_History().AddStates(&sTip, 1);
            flyClient.OnNewTip();
            completedCount = 1; // sender's transaction is completed
            mainReactor->run();

            newReceiverCoins = receiver.GetCoins();

            WALLET_CHECK(newReceiverCoins[0].m_ID.m_Value == 4);
            WALLET_CHECK(newReceiverCoins[0].m_status == Coin::Available);
            WALLET_CHECK(newReceiverCoins[0].m_ID.m_Type == Key::Type::Regular);

            // Tx history check
            rh = receiver.m_WalletDB->getTxHistory();
            WALLET_CHECK(rh.size() == 1);
            rtx = receiver.m_WalletDB->getTx(txId);
            WALLET_CHECK(rtx.is_initialized());

            WALLET_CHECK(rtx->m_status == TxStatus::Completed);
            WALLET_CHECK(rtx->m_sender == false);
        }

        // second transfer
        auto preselectedCoins = sender.m_WalletDB->selectCoins(6);
        CoinIDList preselectedIDs;
        for (const auto& c : preselectedCoins)
        {
            preselectedIDs.push_back(c.m_ID);
        }
        sw.start();

        txId = sender.m_Wallet.StartTransaction(CreateSimpleTransactionParameters()
            .SetParameter(TxParameterID::MyID, sender.m_WalletID)
            .SetParameter(TxParameterID::PeerID, receiver.m_WalletID)
            .SetParameter(TxParameterID::Amount, Amount(6))
            .SetParameter(TxParameterID::Fee, Amount(0))
            .SetParameter(TxParameterID::Lifetime, Height(200))
            .SetParameter(TxParameterID::PreselectedCoins, preselectedIDs));

        mainReactor->run();
        sw.stop();
        cout << "Second transfer elapsed time: " << sw.milliseconds() << " ms\n";

        // check coins
        newSenderCoins = sender.GetCoins();
        newReceiverCoins = receiver.GetCoins();

        WALLET_CHECK(newSenderCoins.size() == 5);
        WALLET_CHECK(newReceiverCoins.size() == 2);

        WALLET_CHECK(newReceiverCoins[0].m_ID.m_Value == 4);
        WALLET_CHECK(newReceiverCoins[0].m_status == Coin::Available);
        WALLET_CHECK(newReceiverCoins[0].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newReceiverCoins[1].m_ID.m_Value == 6);
        WALLET_CHECK(newReceiverCoins[1].m_status == Coin::Available);
        WALLET_CHECK(newReceiverCoins[1].m_ID.m_Type == Key::Type::Regular);


        WALLET_CHECK(newSenderCoins[0].m_ID.m_Value == 5);
        WALLET_CHECK(newSenderCoins[0].m_status == Coin::Spent);
        WALLET_CHECK(newSenderCoins[0].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newSenderCoins[1].m_ID.m_Value == 2);
        WALLET_CHECK(newSenderCoins[1].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[1].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newSenderCoins[2].m_ID.m_Value == 1);
        WALLET_CHECK(newSenderCoins[2].m_status == Coin::Spent);
        WALLET_CHECK(newSenderCoins[2].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newSenderCoins[3].m_ID.m_Value == 9);
        WALLET_CHECK(newSenderCoins[3].m_status == Coin::Spent);
        WALLET_CHECK(newSenderCoins[3].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newSenderCoins[4].m_ID.m_Value == 3);
        WALLET_CHECK(newSenderCoins[4].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[4].m_ID.m_Type == Key::Type::Change);

        // Tx history check
        sh = sender.m_WalletDB->getTxHistory();
        WALLET_CHECK(sh.size() == 2);
        rh = receiver.m_WalletDB->getTxHistory();
        WALLET_CHECK(rh.size() == 2);
        stx = sender.m_WalletDB->getTx(txId);
        WALLET_CHECK(stx.is_initialized());
        rtx = receiver.m_WalletDB->getTx(txId);
        WALLET_CHECK(rtx.is_initialized());

        WALLET_CHECK(stx->m_txId == rtx->m_txId);
        WALLET_CHECK(stx->m_amount == rtx->m_amount);
        WALLET_CHECK(stx->m_status == TxStatus::Completed);
        WALLET_CHECK(stx->m_message == rtx->m_message);
        WALLET_CHECK(stx->m_createTime <= rtx->m_createTime);
        WALLET_CHECK(stx->m_status == rtx->m_status);
        WALLET_CHECK(stx->m_sender == true);
        WALLET_CHECK(rtx->m_sender == false);


        // third transfer. no enough money should appear
        sw.start();
        completedCount = 1;// only one wallet takes part in tx

        txId = sender.m_Wallet.StartTransaction(CreateSimpleTransactionParameters()
            .SetParameter(TxParameterID::MyID, sender.m_WalletID)
            .SetParameter(TxParameterID::PeerID, receiver.m_WalletID)
            .SetParameter(TxParameterID::Amount, Amount(6))
            .SetParameter(TxParameterID::Fee, Amount(0))
            .SetParameter(TxParameterID::Lifetime, Height(200)));

        mainReactor->run();
        sw.stop();
        cout << "Third transfer elapsed time: " << sw.milliseconds() << " ms\n";
    
        // check coins
        newSenderCoins = sender.GetCoins();
        newReceiverCoins = receiver.GetCoins();

        // no coins 
        WALLET_CHECK(newSenderCoins.size() == 5);
        WALLET_CHECK(newReceiverCoins.size() == 2);

        // Tx history check. New failed tx should be added to sender
        sh = sender.m_WalletDB->getTxHistory();
        WALLET_CHECK(sh.size() == 3);
        rh = receiver.m_WalletDB->getTxHistory();
        WALLET_CHECK(rh.size() == 2);
        stx = sender.m_WalletDB->getTx(txId);
        WALLET_CHECK(stx.is_initialized());
        rtx = receiver.m_WalletDB->getTx(txId);
        WALLET_CHECK(!rtx.is_initialized());

        WALLET_CHECK(stx->m_amount == 6);
        WALLET_CHECK(stx->m_status == TxStatus::Failed);
        WALLET_CHECK(stx->m_sender == true);
        WALLET_CHECK(stx->m_failureReason == TxFailureReason::NoInputs);
    }

    void TestTxRollback()
    {
        cout << "\nTesting transaction restore on tip rollback...\n";

        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);

        int completedCount = 2;
        auto f = [&completedCount, mainReactor](auto)
        {
            --completedCount;
            if (completedCount == 0)
            {
                mainReactor->stop();
                completedCount = 2;
            }
        };


        auto senderDB = createSenderWalletDB();
        TestNode node;
        {
            TestWalletRig sender("sender", senderDB, f, TestWalletRig::Type::Regular, false, 0);
            TestWalletRig receiver("receiver", createReceiverWalletDB(), f);

            sender.m_Wallet.StartTransaction(CreateSimpleTransactionParameters()
                .SetParameter(TxParameterID::MyID, sender.m_WalletID)
                .SetParameter(TxParameterID::PeerID, receiver.m_WalletID)
                .SetParameter(TxParameterID::Amount, Amount(4))
                .SetParameter(TxParameterID::Fee, Amount(2))
                .SetParameter(TxParameterID::Lifetime, Height(200))
                .SetParameter(TxParameterID::PeerResponseTime, Height(20)));

            mainReactor->run();

            Block::SystemState::Full tip;
            sender.m_WalletDB->get_History().get_Tip(tip);
            sender.m_WalletDB->get_History().DeleteFrom(tip.m_Height);

            proto::FlyClient& client = sender.m_Wallet;
            client.OnRolledBack();
            // emulating what FlyClient does on rollback
            sender.m_WalletDB->get_History().AddStates(&tip, 1);
            node.AddBlock();
            sender.m_WalletDB->get_History().AddStates(&node.m_Blockchain.m_mcm.m_vStates.back().m_Hdr, 1);
            client.OnNewTip();
        }

       // disconnect wallet from the blockchain for a while
       for (int i = 0; i < 1; ++i)
       {
           node.AddBlock();
       }

        completedCount = 1;
        TestWalletRig sender("sender", senderDB, f, TestWalletRig::Type::Regular, false, 0);
        mainReactor->run();

       // check coins
       vector<Coin> newSenderCoins = sender.GetCoins();
       
        WALLET_CHECK(newSenderCoins[0].m_ID.m_Value == 5);
        WALLET_CHECK(newSenderCoins[0].m_status == Coin::Spent);
        WALLET_CHECK(newSenderCoins[0].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newSenderCoins[1].m_ID.m_Value == 2);
        WALLET_CHECK(newSenderCoins[1].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[1].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newSenderCoins[2].m_ID.m_Value == 1);
        WALLET_CHECK(newSenderCoins[2].m_status == Coin::Spent);
        WALLET_CHECK(newSenderCoins[2].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newSenderCoins[3].m_ID.m_Value == 9);
        WALLET_CHECK(newSenderCoins[3].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[3].m_ID.m_Type == Key::Type::Regular);
    }

    void TestSplitTransaction()
    {
        cout << "\nTesting split Tx...\n";

        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);

        auto senderWalletDB = createSqliteWalletDB("sender_wallet.db", false);

        // add coin with keyType - Coinbase
        beam::Amount coin_amount = 40;
        Coin coin = CreateAvailCoin(coin_amount, 0);
        coin.m_ID.m_Type = Key::Type::Coinbase;
        senderWalletDB->storeCoin(coin);

        auto coins = senderWalletDB->selectCoins(24);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_ID.m_Type == Key::Type::Coinbase);
        WALLET_CHECK(coins[0].m_status == Coin::Available);
        WALLET_CHECK(senderWalletDB->getTxHistory().empty());

        TestNode node;
        TestWalletRig sender("sender", senderWalletDB, [](auto) { io::Reactor::get_Current().stop(); });
        helpers::StopWatch sw;

        sw.start();

        auto txId = sender.m_Wallet.StartTransaction(CreateSplitTransactionParameters(sender.m_WalletID, AmountList{ 11, 12, 13 })
            .SetParameter(TxParameterID::Fee, Amount(2))
            .SetParameter(TxParameterID::Lifetime, Height(200)));

        mainReactor->run();
        sw.stop();

        cout << "Transfer elapsed time: " << sw.milliseconds() << " ms\n";

        // check Tx
        auto txHistory = senderWalletDB->getTxHistory();
        WALLET_CHECK(txHistory.size() == 1);
        WALLET_CHECK(txHistory[0].m_txId == txId);
        WALLET_CHECK(txHistory[0].m_amount == 36);
        WALLET_CHECK(txHistory[0].m_change == 2);
        WALLET_CHECK(txHistory[0].m_fee == 2);
        WALLET_CHECK(txHistory[0].m_status == TxStatus::Completed);

        // check coins
        vector<Coin> newSenderCoins;
        senderWalletDB->visitCoins([&newSenderCoins](const Coin& c)->bool
        {
            newSenderCoins.push_back(c);
            return true;
        });

        WALLET_CHECK(newSenderCoins.size() == 5);
        WALLET_CHECK(newSenderCoins[0].m_ID.m_Type == Key::Type::Coinbase);
        WALLET_CHECK(newSenderCoins[0].m_status == Coin::Spent);
        WALLET_CHECK(newSenderCoins[0].m_ID.m_Value == 40);

        WALLET_CHECK(newSenderCoins[1].m_ID.m_Type == Key::Type::Change);
        WALLET_CHECK(newSenderCoins[1].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[1].m_ID.m_Value == 2);

        WALLET_CHECK(newSenderCoins[2].m_ID.m_Type == Key::Type::Regular);
        WALLET_CHECK(newSenderCoins[2].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[2].m_ID.m_Value == 11);

        WALLET_CHECK(newSenderCoins[3].m_ID.m_Type == Key::Type::Regular);
        WALLET_CHECK(newSenderCoins[3].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[3].m_ID.m_Value == 12);

        WALLET_CHECK(newSenderCoins[4].m_ID.m_Type == Key::Type::Regular);
        WALLET_CHECK(newSenderCoins[4].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[4].m_ID.m_Value == 13);

        cout << "\nFinish of testing split Tx...\n";
    }

    void TestMinimalFeeTransaction()
    {
        struct ForkHolder
        {
            ForkHolder(Height h)
                : m_PrevValue{ Rules::get().pForks[1].m_Height }
            {
                Rules::get().pForks[1].m_Height = h;
                Rules::get().UpdateChecksum();
            }

            ~ForkHolder()
            {
                Rules::get().pForks[1].m_Height = m_PrevValue;
                Rules::get().UpdateChecksum();
            }

            Height m_PrevValue;
        };

        ForkHolder holder(140); // set fork height

        cout << "\nTesting minimal Tx...\n";

        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);

        auto senderWalletDB = createSqliteWalletDB("sender_wallet.db", false);

        // add coin with keyType - Coinbase
        Coin coin = CreateAvailCoin(100, 0);
        coin.m_ID.m_Type = Key::Type::Coinbase;
        senderWalletDB->storeCoin(coin);

        auto coins = senderWalletDB->selectCoins(24);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_ID.m_Type == Key::Type::Coinbase);
        WALLET_CHECK(coins[0].m_status == Coin::Available);
        WALLET_CHECK(senderWalletDB->getTxHistory().empty());

        TestNode node;
        TestWalletRig sender("sender", senderWalletDB, [](auto) { io::Reactor::get_Current().stop(); });


        auto txId = sender.m_Wallet.StartTransaction(CreateSplitTransactionParameters(sender.m_WalletID, AmountList{ 11, 12, 13 })
           .SetParameter(TxParameterID::Fee, Amount(2))
           .SetParameter(TxParameterID::Lifetime, Height(200)));

        mainReactor->run();

        // check Tx
        {
            auto txHistory = senderWalletDB->getTxHistory();
            WALLET_CHECK(txHistory.size() == 1);
            WALLET_CHECK(txHistory[0].m_txId == txId);
            WALLET_CHECK(txHistory[0].m_amount == 36);
            WALLET_CHECK(txHistory[0].m_change == 0);
            WALLET_CHECK(txHistory[0].m_fee == 2);
            WALLET_CHECK(txHistory[0].m_status == TxStatus::Failed);
        }
        

        txId = sender.m_Wallet.StartTransaction(CreateSplitTransactionParameters(sender.m_WalletID, AmountList{ 11, 12, 13 })
            .SetParameter(TxParameterID::Fee, Amount(42))
            .SetParameter(TxParameterID::Lifetime, Height(200)));
        mainReactor->run();

        // check Tx
        {
            auto txHistory = senderWalletDB->getTxHistory();
            WALLET_CHECK(txHistory.size() == 2);
            auto tx = *senderWalletDB->getTx(txId);
            WALLET_CHECK(tx.m_txId == txId);
            WALLET_CHECK(tx.m_amount == 36);
            WALLET_CHECK(tx.m_change == 0);
            WALLET_CHECK(tx.m_fee == 42);
            WALLET_CHECK(tx.m_status == TxStatus::Failed);
        }

        // another attempt
        txId = sender.m_Wallet.StartTransaction(CreateSplitTransactionParameters(sender.m_WalletID, AmountList{ 11, 12, 13 })
            .SetParameter(TxParameterID::Fee, Amount(50))
            .SetParameter(TxParameterID::Lifetime, Height(200)));
        mainReactor->run();

        // check Tx
        {
            auto tx = senderWalletDB->getTx(txId);
            WALLET_CHECK(tx);
            WALLET_CHECK(tx->m_txId == txId);
            WALLET_CHECK(tx->m_amount == 36);
            WALLET_CHECK(tx->m_change == 14);
            WALLET_CHECK(tx->m_fee == 50);
            WALLET_CHECK(tx->m_status == TxStatus::Completed);
        }

        // check coins
        vector<Coin> newSenderCoins = sender.GetCoins();

        WALLET_CHECK(newSenderCoins.size() == 5);
        WALLET_CHECK(newSenderCoins[0].m_ID.m_Type == Key::Type::Coinbase);
        WALLET_CHECK(newSenderCoins[0].m_status == Coin::Spent);
        WALLET_CHECK(newSenderCoins[0].m_ID.m_Value == 100);

        WALLET_CHECK(newSenderCoins[1].m_ID.m_Type == Key::Type::Change);
        WALLET_CHECK(newSenderCoins[1].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[1].m_ID.m_Value == 14);

        WALLET_CHECK(newSenderCoins[2].m_ID.m_Type == Key::Type::Regular);
        WALLET_CHECK(newSenderCoins[2].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[2].m_ID.m_Value == 11);

        WALLET_CHECK(newSenderCoins[3].m_ID.m_Type == Key::Type::Regular);
        WALLET_CHECK(newSenderCoins[3].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[3].m_ID.m_Value == 12);

        WALLET_CHECK(newSenderCoins[4].m_ID.m_Type == Key::Type::Regular);
        WALLET_CHECK(newSenderCoins[4].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[4].m_ID.m_Value == 13);
    }

    void TestExpiredTransaction()
    {
        cout << "\nTesting expired Tx...\n";

        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);

        int completedCount = 2;
        auto f = [&completedCount, mainReactor](auto)
        {
            --completedCount;
            if (completedCount == 0)
            {
                mainReactor->stop();
                completedCount = 2;
            }
        };

        TestWalletRig sender("sender", createSenderWalletDB(), f);
        TestWalletRig receiver("receiver", createReceiverWalletDB(), f, TestWalletRig::Type::Offline);

        auto newBlockFunc = [&receiver](Height height)
        {
            if (height == 200)
            {
                auto nodeEndpoint = make_shared<proto::FlyClient::NetworkStd>(receiver.m_Wallet);
                nodeEndpoint->m_Cfg.m_vNodes.push_back(io::Address::localhost().port(32125));
                nodeEndpoint->Connect();
                receiver.m_Wallet.AddMessageEndpoint(make_shared<WalletNetworkViaBbs>(receiver.m_Wallet, nodeEndpoint, receiver.m_WalletDB, receiver.m_KeyKeeper));
                receiver.m_Wallet.SetNodeEndpoint(nodeEndpoint);
            }
        };

        TestNode node(newBlockFunc);
        io::Timer::Ptr timer = io::Timer::create(*mainReactor);
        timer->start(1000, true, [&node]() {node.AddBlock(); });

        WALLET_CHECK(sender.m_WalletDB->selectCoins(6).size() == 2);
        WALLET_CHECK(sender.m_WalletDB->getTxHistory().empty());
        WALLET_CHECK(receiver.m_WalletDB->getTxHistory().empty());

        auto txId = sender.m_Wallet.StartTransaction(CreateSimpleTransactionParameters()
            .SetParameter(TxParameterID::MyID, sender.m_WalletID)
            .SetParameter(TxParameterID::PeerID, receiver.m_WalletID)
            .SetParameter(TxParameterID::Amount, Amount(4))
            .SetParameter(TxParameterID::Fee, Amount(2))
            .SetParameter(TxParameterID::Lifetime, Height(0))
            .SetParameter(TxParameterID::PeerResponseTime, Height(10)));

        mainReactor->run();

        // first tx with height == 0
        {
            vector<Coin> newSenderCoins = sender.GetCoins();
            vector<Coin> newReceiverCoins = receiver.GetCoins();

            WALLET_CHECK(newSenderCoins.size() == 4);
            WALLET_CHECK(newReceiverCoins.size() == 0);

            auto sh = sender.m_WalletDB->getTxHistory();
            WALLET_CHECK(sh.size() == 1);
            WALLET_CHECK(sh[0].m_status == TxStatus::Failed);
            WALLET_CHECK(sh[0].m_failureReason == TxFailureReason::TransactionExpired);
            auto rh = receiver.m_WalletDB->getTxHistory();
            WALLET_CHECK(rh.size() == 1);
            WALLET_CHECK(rh[0].m_status == TxStatus::Failed);
            WALLET_CHECK(rh[0].m_failureReason == TxFailureReason::TransactionExpired);
        }

        txId = sender.m_Wallet.StartTransaction(CreateSimpleTransactionParameters()
            .SetParameter(TxParameterID::MyID, sender.m_WalletID)
            .SetParameter(TxParameterID::PeerID, receiver.m_WalletID)
            .SetParameter(TxParameterID::Amount, Amount(4))
            .SetParameter(TxParameterID::Fee, Amount(2)));

        mainReactor->run();

        {
            vector<Coin> newSenderCoins = sender.GetCoins();
            vector<Coin> newReceiverCoins = receiver.GetCoins();

            WALLET_CHECK(newSenderCoins.size() == 4);
            WALLET_CHECK(newReceiverCoins.size() == 1);

            auto sh = sender.m_WalletDB->getTxHistory();
            WALLET_CHECK(sh.size() == 2);
            auto sit = find_if(sh.begin(), sh.end(), [&txId](const auto& t) {return t.m_txId == txId; });
            WALLET_CHECK(sit->m_status == TxStatus::Completed);
            auto rh = receiver.m_WalletDB->getTxHistory();
            WALLET_CHECK(rh.size() == 2);
            auto rit = find_if(rh.begin(), rh.end(), [&txId](const auto& t) {return t.m_txId == txId; });
            WALLET_CHECK(rit->m_status == TxStatus::Completed);
        }
    }

    void TestTransactionUpdate()
    {
        cout << "\nTesting transaction update ...\n";

        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);
        EmptyTestGateway gateway;
        TestWalletRig sender("sender", createSenderWalletDB());
        TestWalletRig receiver("receiver", createReceiverWalletDB());

        TxID txID = wallet::GenerateTxID();
        SimpleTransaction::Creator simpleCreator(sender.m_WalletDB);
        BaseTransaction::Creator& creator = simpleCreator;
        auto tx = creator.Create(gateway, sender.m_WalletDB, sender.m_KeyKeeper, txID);

        Height currentHeight = sender.m_WalletDB->getCurrentHeight();

        tx->SetParameter(wallet::TxParameterID::TransactionType, wallet::TxType::Simple, false);
        tx->SetParameter(wallet::TxParameterID::MaxHeight, currentHeight + 2, false); // transaction is valid +lifetime blocks from currentHeight
        tx->SetParameter(wallet::TxParameterID::IsInitiator, true, false);
        //tx->SetParameter(wallet::TxParameterID::AmountList, {1U}, false);
      //  tx->SetParameter(wallet::TxParameterID::PreselectedCoins, {}, false);

        TxDescription txDescription(txID);

        txDescription.m_txId = txID;
        txDescription.m_amount = 1;
        txDescription.m_fee = 2;
        txDescription.m_minHeight = currentHeight;
        txDescription.m_peerId = receiver.m_WalletID;
        txDescription.m_myId = sender.m_WalletID;
        txDescription.m_message = {};
        txDescription.m_createTime = getTimestamp();
        txDescription.m_sender = true;
        txDescription.m_status = TxStatus::Pending;
        txDescription.m_selfTx = false;
        sender.m_WalletDB->saveTx(txDescription);
        
        const int UpdateCount = 100000;
        helpers::StopWatch sw;
        sw.start();
        for (int i = 0; i < UpdateCount; ++i)
        {
            tx->Update();
        }
        sw.stop();

        cout << UpdateCount << " updates: " << sw.milliseconds() << " ms\n";

    }

    void TestTxExceptionHandling()
    {
        cout << "\nTesting exception processing by transaction ...\n";

        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);

        TestWalletRig sender("sender", createSenderWalletDB());
        TestWalletRig receiver("receiver", createReceiverWalletDB());
        Height currentHeight = sender.m_WalletDB->getCurrentHeight();

        SimpleTransaction::Creator simpleTxCreator(sender.m_WalletDB);
        BaseTransaction::Creator& txCreator = simpleTxCreator;
        // process TransactionFailedException
        {
            struct TestGateway : EmptyTestGateway
            {
                bool get_tip(Block::SystemState::Full& state) const override
                {
                    throw wallet::TransactionFailedException(true, TxFailureReason::FailedToGetParameter);
                }
            } gateway;

            TxID txID = wallet::GenerateTxID();
            auto tx = txCreator.Create(gateway, sender.m_WalletDB, sender.m_KeyKeeper, txID);

            tx->SetParameter(wallet::TxParameterID::TransactionType, wallet::TxType::Simple, false);
            tx->SetParameter(wallet::TxParameterID::MaxHeight, currentHeight + 2, false); // transaction is valid +lifetime blocks from currentHeight
            tx->SetParameter(wallet::TxParameterID::IsInitiator, true, false);

            TxDescription txDescription(txID);

            txDescription.m_txId = txID;
            txDescription.m_amount = 1;
            txDescription.m_fee = 2;
            txDescription.m_minHeight = currentHeight;
            txDescription.m_peerId = receiver.m_WalletID;
            txDescription.m_myId = sender.m_WalletID;
            txDescription.m_message = {};
            txDescription.m_createTime = getTimestamp();
            txDescription.m_sender = true;
            txDescription.m_status = TxStatus::Pending;
            txDescription.m_selfTx = false;
            sender.m_WalletDB->saveTx(txDescription);

            tx->Update();

            auto result = sender.m_WalletDB->getTx(txID);

            WALLET_CHECK(result->m_status == TxStatus::Failed);
        }

        // process unknown exception
        {
            struct TestGateway : EmptyTestGateway
            {
                bool get_tip(Block::SystemState::Full& state) const override
                {
                    throw exception();
                }
            } gateway;

            TxID txID = wallet::GenerateTxID();
            auto tx = txCreator.Create(gateway, sender.m_WalletDB, sender.m_KeyKeeper, txID);

            tx->SetParameter(wallet::TxParameterID::TransactionType, wallet::TxType::Simple, false);
            tx->SetParameter(wallet::TxParameterID::MaxHeight, currentHeight + 2, false); // transaction is valid +lifetime blocks from currentHeight
            tx->SetParameter(wallet::TxParameterID::IsInitiator, true, false);

            TxDescription txDescription(txID);

            txDescription.m_txId = txID;
            txDescription.m_amount = 1;
            txDescription.m_fee = 2;
            txDescription.m_minHeight = currentHeight;
            txDescription.m_peerId = receiver.m_WalletID;
            txDescription.m_myId = sender.m_WalletID;
            txDescription.m_message = {};
            txDescription.m_createTime = getTimestamp();
            txDescription.m_sender = true;
            txDescription.m_status = TxStatus::Pending;
            txDescription.m_selfTx = false;
            sender.m_WalletDB->saveTx(txDescription);

            tx->Update();

            auto result = sender.m_WalletDB->getTx(txID);

            WALLET_CHECK(result->m_status == TxStatus::Failed);
        }
    }

    void TestTxPerformance()
    {
        cout << "\nTesting tx performance...\n";

        const int MaxTxCount = 100;// 00;
        vector<PerformanceRig> tests;

        for (int i = 10; i <= MaxTxCount; i *= 10)
        {
            /*for (int j = 1; j <= 5; ++j)
            {
                tests.emplace_back(i, j);
            }*/
            tests.emplace_back(i, 1);
            tests.emplace_back(i, i);
        }

        for (auto& t : tests)
        {
            t.Run();
        }

        for (auto& t : tests)
        {
            cout << "Transferring of " << t.GetTxCount() << " by " << t.GetTxPerCall() << " transactions per call took: " << t.GetTotalTime() << " ms Max api latency: " << t.GetMaxLatency() << endl;
        }
    }

    void TestTxNonces()
    {
        cout << "\nTesting tx nonce...\n";

        PerformanceRig t2(200);
        t2.Run();
        t2.Run();

    }

    void TestColdWalletSending()
    {
        cout << "\nTesting cold wallet sending...\n";

        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);

        int completedCount = 2;
        auto f = [&completedCount, mainReactor](auto)
        {
            --completedCount;
            if (completedCount == 0)
            {
                mainReactor->stop();
                completedCount = 2;
            }
        };

        TestNode node;
        TestWalletRig receiver("receiver", createReceiverWalletDB(), f);
        {
            TestWalletRig privateSender("sender", createSenderWalletDB(true), f, TestWalletRig::Type::ColdWallet);
            WALLET_CHECK(privateSender.m_WalletDB->selectCoins(6).size() == 2);
            WALLET_CHECK(privateSender.m_WalletDB->getTxHistory().empty());

            // send from cold wallet

            privateSender.m_Wallet.StartTransaction(CreateSimpleTransactionParameters()
                .SetParameter(TxParameterID::MyID, privateSender.m_WalletID)
                .SetParameter(TxParameterID::PeerID, receiver.m_WalletID)
                .SetParameter(TxParameterID::Amount, Amount(4))
                .SetParameter(TxParameterID::Fee, Amount(2))
                .SetParameter(TxParameterID::Lifetime, Height(200)));

            mainReactor->run();
        }

        string publicPath = "sender_public.db";
        {
            // cold -> hot
            boost::filesystem::remove(publicPath);
            boost::filesystem::copy_file(SenderWalletDB, publicPath);

            auto publicDB = WalletDB::open(publicPath, DBPassword, io::Reactor::get_Current().shared_from_this());
            TestWalletRig publicSender("public_sender", publicDB, f, TestWalletRig::Type::Regular, true);

            WALLET_CHECK(publicSender.m_WalletDB->getTxHistory().size() == 1);
            WALLET_CHECK(receiver.m_WalletDB->getTxHistory().empty());

            mainReactor->run();
        }

        {
            // hot -> cold
            boost::filesystem::remove(SenderWalletDB);
            boost::filesystem::copy_file(publicPath, SenderWalletDB);
            auto privateDB = WalletDB::open(SenderWalletDB, DBPassword, io::Reactor::get_Current().shared_from_this());
            TestWalletRig privateSender("sender", privateDB, f, TestWalletRig::Type::ColdWallet);
            mainReactor->run();
        }

        // cold -> hot
        boost::filesystem::remove(publicPath);
        boost::filesystem::copy_file(SenderWalletDB, publicPath);

        auto publicDB = WalletDB::open(publicPath, DBPassword, io::Reactor::get_Current().shared_from_this());
        TestWalletRig publicSender("public_sender", publicDB, f);

        mainReactor->run();

        // check coins
        vector<Coin> newSenderCoins = publicSender.GetCoins();
        vector<Coin> newReceiverCoins = receiver.GetCoins();

        WALLET_CHECK(newSenderCoins.size() == 4);
        WALLET_CHECK(newReceiverCoins.size() == 1);
        WALLET_CHECK(newReceiverCoins[0].m_ID.m_Value == 4);
        WALLET_CHECK(newReceiverCoins[0].m_status == Coin::Available);
        WALLET_CHECK(newReceiverCoins[0].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newSenderCoins[0].m_ID.m_Value == 5);
        WALLET_CHECK(newSenderCoins[0].m_status == Coin::Spent);
        WALLET_CHECK(newSenderCoins[0].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newSenderCoins[1].m_ID.m_Value == 2);
        WALLET_CHECK(newSenderCoins[1].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[1].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newSenderCoins[2].m_ID.m_Value == 1);
        WALLET_CHECK(newSenderCoins[2].m_status == Coin::Spent);
        WALLET_CHECK(newSenderCoins[2].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newSenderCoins[3].m_ID.m_Value == 9);
        WALLET_CHECK(newSenderCoins[3].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[3].m_ID.m_Type == Key::Type::Regular);

    }


    void TestColdWalletReceiving()
    {
        cout << "\nTesting cold wallet receiving...\n";

        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);

        int completedCount = 2;
        auto f = [&completedCount, mainReactor](auto)
        {
            --completedCount;
            if (completedCount == 0)
            {
                mainReactor->stop();
                completedCount = 2;
            }
        };

        TestNode node;
        TestWalletRig sender("sender", createSenderWalletDB(), f);

        {
            // create cold wallet
            TestWalletRig privateReceiver("receiver", createReceiverWalletDB(true), f, TestWalletRig::Type::ColdWallet);
        }

        string publicPath = "receiver_public.db";
        {
            // cold -> hot
            boost::filesystem::remove(publicPath);
            boost::filesystem::copy_file(ReceiverWalletDB, publicPath);

            auto publicDB = WalletDB::open(publicPath, DBPassword, io::Reactor::get_Current().shared_from_this());
            TestWalletRig publicReceiver("public_receiver", publicDB, f, TestWalletRig::Type::Regular, true);

            sender.m_Wallet.StartTransaction(CreateSimpleTransactionParameters()
                .SetParameter(TxParameterID::MyID, sender.m_WalletID)
                .SetParameter(TxParameterID::PeerID, publicReceiver.m_WalletID)
                .SetParameter(TxParameterID::Amount, Amount(4))
                .SetParameter(TxParameterID::Fee, Amount(2))
                .SetParameter(TxParameterID::Lifetime, Height(200)));

            mainReactor->run();
        }

        {
            // hot -> cold
            boost::filesystem::remove(ReceiverWalletDB);
            boost::filesystem::copy_file(publicPath, ReceiverWalletDB);
            auto privateDB = WalletDB::open(ReceiverWalletDB, DBPassword, io::Reactor::get_Current().shared_from_this());
            TestWalletRig privateReceiver("receiver", privateDB, f, TestWalletRig::Type::ColdWallet);
            mainReactor->run();
        }

        {
            // cold -> hot
            boost::filesystem::remove(publicPath);
            boost::filesystem::copy_file(ReceiverWalletDB, publicPath);

            auto publicDB = WalletDB::open(publicPath, DBPassword, io::Reactor::get_Current().shared_from_this());
            TestWalletRig publicReceiver("public_receiver", publicDB, f);

            mainReactor->run();
        }

        // hot -> cold
        boost::filesystem::remove(ReceiverWalletDB);
        boost::filesystem::copy_file(publicPath, ReceiverWalletDB);
        auto privateDB = WalletDB::open(ReceiverWalletDB, DBPassword, io::Reactor::get_Current().shared_from_this());
        TestWalletRig privateReceiver("receiver", privateDB, f, TestWalletRig::Type::ColdWallet);

        // check coins
        vector<Coin> newSenderCoins = sender.GetCoins();
        vector<Coin> newReceiverCoins = privateReceiver.GetCoins();

        WALLET_CHECK(newSenderCoins.size() == 4);
        WALLET_CHECK(newReceiverCoins.size() == 1);
        WALLET_CHECK(newReceiverCoins[0].m_ID.m_Value == 4);
        WALLET_CHECK(newReceiverCoins[0].m_status == Coin::Available);
        WALLET_CHECK(newReceiverCoins[0].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newSenderCoins[0].m_ID.m_Value == 5);
        WALLET_CHECK(newSenderCoins[0].m_status == Coin::Spent);
        WALLET_CHECK(newSenderCoins[0].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newSenderCoins[1].m_ID.m_Value == 2);
        WALLET_CHECK(newSenderCoins[1].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[1].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newSenderCoins[2].m_ID.m_Value == 1);
        WALLET_CHECK(newSenderCoins[2].m_status == Coin::Spent);
        WALLET_CHECK(newSenderCoins[2].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newSenderCoins[3].m_ID.m_Value == 9);
        WALLET_CHECK(newSenderCoins[3].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[3].m_ID.m_Type == Key::Type::Regular);

    }

    uintBig GetRandomSeed()
    {
        uintBig seed;
        std::generate_n(&seed.m_pData[0], seed.nBytes, []() {return uint8_t(std::rand() % 256); });
        return seed;
    }

    void TestBbsMessages()
    {
        printf("Testing bbs ...\n");
        io::Reactor::Ptr mainReactor(io::Reactor::create());
        io::Reactor::Scope scope(*mainReactor);
        const int Count = 500;
        string nodePath = "node.db";
        if (boost::filesystem::exists(nodePath))
        {
            boost::filesystem::remove(nodePath);
        }

        struct MyFlyClient : public proto::FlyClient
            , public IWalletMessageConsumer
        {
            MyFlyClient(IWalletDB::Ptr db, const WalletID& receiverID, IPrivateKeyKeeper::Ptr keyKeeper)
                : m_Nnet(make_shared<proto::FlyClient::NetworkStd>(*this))
                , m_Bbs(*this, m_Nnet, db, keyKeeper)
                , m_ReceiverID(receiverID)
            {
                WalletAddress wa = storage::createAddress(*db, keyKeeper);
                db->saveAddress(wa);
                m_WalletID = wa.m_walletID;
            }

            void Connect(io::Address address)
            {
                m_Nnet->m_Cfg.m_vNodes.push_back(address);
                m_Nnet->Connect();
            }

            Block::SystemState::IHistory& get_History() override
            {
                return m_Headers;
            }

            void OnWalletMessage(const WalletID& peerID, const SetTxParameter& p) override
            {
            }

            void SendMessage()
            {
                IWalletMessageEndpoint& endpoint = m_Bbs;
                ByteBuffer message;
                std::generate_n(std::back_inserter(message), 10000, []() { return uint8_t(std::rand() % 256); });
                //endpoint.SendEncryptedMessage(m_ReceiverID, message);
                SetTxParameter params;
                params.m_From = m_WalletID;
                params.m_Type = TxType::Simple;
                params.m_TxID = wallet::GenerateTxID();
                params.AddParameter(TxParameterID::Inputs, message);

                endpoint.Send(m_ReceiverID, params);
            }

            Block::SystemState::HistoryMap m_Headers;
            std::shared_ptr<proto::FlyClient::NetworkStd> m_Nnet;
            WalletNetworkViaBbs m_Bbs;
            WalletID m_ReceiverID;
            WalletID m_WalletID;
        };

        struct SenderFlyClient : public MyFlyClient
        {
            SenderFlyClient(IWalletDB::Ptr db, const WalletID& receiverID, IPrivateKeyKeeper::Ptr keyKeeper)
                : MyFlyClient(db, receiverID, keyKeeper)
                , m_Timer(io::Timer::create(io::Reactor::get_Current()))
            {
            }

            void OnNewTip() override
            {
                if (!m_Sent)
                {
                    m_Sent = true;
                    
                    for (int i = 0; i < Count; ++i)
                    {
                        SendMessage();
                    }
                }
            }

            void OnWalletMessage(const WalletID& peerID, const SetTxParameter& p) override
            {
                ++m_ReceivedCount;
                cout << "Response message received[" << m_ReceivedCount << "] : " << p.m_TxID << '\n';

                m_Timer->start(60000, false, [this]() {OnTimer(); });
                if (m_ReceivedCount == Count)
                {
                    m_Timer->cancel();
                    io::Reactor::get_Current().stop();
                }
            }

            void OnTimer()
            {
                io::Reactor::get_Current().stop();
                WALLET_CHECK(m_ReceivedCount == Count);
            }

            bool m_Sent = false;
            
            io::Timer::Ptr m_Timer;
            int m_ReceivedCount = 0;
        };


        struct ReceiverFlyClient : public MyFlyClient
        {
            ReceiverFlyClient(IWalletDB::Ptr db, const WalletID& receiverID, IPrivateKeyKeeper::Ptr keyKeeper)
                : MyFlyClient(db, receiverID, keyKeeper)
            {
                
            }

            void OnWalletMessage(const WalletID& peerID, const SetTxParameter& p) override
            {
                ++m_ReceivedCount;
                cout << "Message received[" << m_ReceivedCount<< "] : " << p.m_TxID << '\n';
                
                SendMessage();
            }

            int m_ReceivedCount = 0;
        };


        auto db = createSqliteWalletDB(SenderWalletDB, false);
        auto treasury = createTreasury(db);

        auto nodeCreator = [](Node& node, const ByteBuffer& treasury, uint16_t port, const std::string& path, const std::vector<io::Address>& peers = {})->io::Address
        {
            InitNodeToTest(node, treasury, nullptr, port, 10000, path, peers);
            io::Address address;
            address.resolve("127.0.0.1");
            address.port(port);
            return address;
        };

        Node senderNode;
        auto senderNodeAddress= nodeCreator(senderNode, treasury, 32125, "sender_node.db");
        Node receiverNode;
        auto receiverNodeAddress = nodeCreator(receiverNode, treasury, 32126, "receiver_node.db", {senderNodeAddress});

        TestWalletRig receiver("receiver", createReceiverWalletDB(), [](auto) {});

        auto senderKeyKeeper = std::make_shared<LocalPrivateKeyKeeper>(db, db->get_MasterKdf());
        SenderFlyClient flyClient(db, receiver.m_WalletID, senderKeyKeeper);
        flyClient.Connect(senderNodeAddress);

        auto receiverKeyKeeper = std::make_shared<LocalPrivateKeyKeeper>(receiver.m_WalletDB, receiver.m_WalletDB->get_MasterKdf());
        ReceiverFlyClient flyClinet2(receiver.m_WalletDB, flyClient.m_WalletID, receiverKeyKeeper);
        flyClinet2.Connect(receiverNodeAddress);

        mainReactor->run();
    }

    void TestBbsMessages2()
    {
        printf("Testing bbs with wallets2 ...\n");
        io::Reactor::Ptr mainReactor(io::Reactor::create());
        io::Reactor::Scope scope(*mainReactor);
        const int Count = 1000;
        string nodePath = "node.db";
        if (boost::filesystem::exists(nodePath))
        {
            boost::filesystem::remove(nodePath);
        }

        int completedCount = 1;

        auto timer = io::Timer::create(io::Reactor::get_Current());
        auto f = [&completedCount, mainReactor](auto txID)
        {
            --completedCount;
            if (completedCount == 0)
            {
              //  timer->cancel();
                mainReactor->stop();
            }
        };

        auto db = createSqliteWalletDB(SenderWalletDB, false);
        auto treasury = createTreasury(db, AmountList{Amount(5*Count)});

        auto nodeCreator = [](Node& node, const ByteBuffer& treasury, uint16_t port, const std::string& path, const std::vector<io::Address>& peers = {}, bool miningNode = true)->io::Address
        {
            InitNodeToTest(node, treasury, nullptr, port, 10000, path, peers, miningNode);
            io::Address address;
            address.resolve("127.0.0.1");
            address.port(port);
            return address;
        };


        Node senderNode;
        auto senderNodeAddress = nodeCreator(senderNode, treasury, 32125, "sender_node.db");
        Node receiverNode;
        auto receiverNodeAddress = nodeCreator(receiverNode, treasury, 32126, "receiver_node.db", { senderNodeAddress }, false);

        TestWalletRig sender("sender", db, f, TestWalletRig::Type::Regular, false, 0, senderNodeAddress);
        TestWalletRig receiver("receiver", createReceiverWalletDB(), f, TestWalletRig::Type::Regular, false, 0, receiverNodeAddress);

        sender.m_Wallet.StartTransaction(CreateSplitTransactionParameters(sender.m_WalletID, AmountList(Count, Amount(5)))
            .SetParameter(TxParameterID::Fee, Amount(0))
            .SetParameter(TxParameterID::Lifetime, Height(200)));

        mainReactor->run();

        sender.m_Wallet.SetBufferSize(10);

        completedCount = 2 * Count;

        for (int i = 0; i < Count; ++i)
        {
            sender.m_Wallet.StartTransaction(CreateSimpleTransactionParameters()
                .SetParameter(TxParameterID::MyID, sender.m_WalletID)
                .SetParameter(TxParameterID::PeerID, receiver.m_WalletID)
                .SetParameter(TxParameterID::Amount, Amount(4))
                .SetParameter(TxParameterID::Fee, Amount(1))
                .SetParameter(TxParameterID::Lifetime, Height(30))
                .SetParameter(TxParameterID::PeerResponseTime, Height(100)));
        }
        
        mainReactor->run();

        //
        //for (const auto& p : txMap)
        //{
        //    if (p.second != 2)
        //    {
        //        cout << p.first << '\n';
        //    }
        //}

        auto transactions = sender.m_WalletDB->getTxHistory();
        WALLET_CHECK(transactions.size() == Count + 1);
        for (const auto& t : transactions)
        {
            WALLET_CHECK(t.m_status == TxStatus::Completed);
        }
    }

    void TestTxParameters()
    {
        std::cout << "Testing tx parameters and token...\n";

        WalletID myID(Zero);
        WALLET_CHECK(myID.FromHex("7a3b9afd0f6bba147a4e044329b135424ca3a57ab9982fe68747010a71e0cac3f3"));

        WalletID peerID(Zero);
        WALLET_CHECK(peerID.FromHex("1b516fb39884a3281bc0761f97817782a8bc51fdb1336882a2c7efebdb400d00d4"));
        auto params = CreateSimpleTransactionParameters()
            .SetParameter(TxParameterID::MyID, myID)
            .SetParameter(TxParameterID::PeerID, peerID);

        string token = to_string(params);

        auto restoredParams = wallet::ParseParameters(token);

        WALLET_CHECK(restoredParams && *restoredParams == params);

        string address = to_string(myID);
        auto addrParams = wallet::ParseParameters(address);
        WALLET_CHECK(addrParams && *addrParams->GetParameter<WalletID>(TxParameterID::PeerID) == myID);

        const string addresses[] =
        {
            "7a3b9afd0f6bba147a4e044329b135424ca3a57ab9982fe68747010a71e0cac3f3",
            "9f03ab404a243fd09f827e8941e419e523a5b21e17c70563bfbc211dbe0e87ca95",
            "0103ab404a243fd09f827e8941e419e523a5b21e17c70563bfbc211dbe0e87ca95",
            "7f9f03ab404a243fd09f827e8941e419e523a5b21e17c70563bfbc211dbe0e87ca95",
            "0f9f03ab404a243fd09f827e8941e419e523a5b21e17c70563bfbc211dbe0e87ca95"
        };
        for (const auto& a : addresses)
        {
            WalletID id(Zero);
            WALLET_CHECK(id.FromHex(a));
            boost::optional<TxParameters> p;
            WALLET_CHECK_NO_THROW(p = wallet::ParseParameters(a));
            WALLET_CHECK(p && *p->GetParameter<WalletID>(TxParameterID::PeerID) == id);
        }
    }

    void TestConvertions()
    {
        //{
        //    vector<uint8_t> input = { 2, 5, 6, 7 };
        //    auto r58 = Convert(input, 10, 58);
        //    WALLET_CHECK(r58 == vector<uint8_t>({44, 15}));
        //    auto r256 = Convert(input, 10, 256);
        //    WALLET_CHECK(r256 == vector<uint8_t>({ 10, 7 }));

        //    auto r58_10 = Convert(r58, 58, 10);
        //    WALLET_CHECK(r58_10 == input);
        //    auto r256_10 = Convert(r256, 256, 10);
        //    WALLET_CHECK(r256_10 == vector<uint8_t>({2, 5, 6, 7}));

        //    auto r11 = Convert(input, 10, 11);
        //    WALLET_CHECK(r11 == vector<uint8_t>({ 1, 10, 2, 4 }));

        //    auto r58_11 = Convert(r58, 58, 11);
        //    WALLET_CHECK(r58_11 == vector<uint8_t>({ 1, 10, 2, 4 }));
        //    auto r256_11 = Convert(r256, 256, 11);
        //    WALLET_CHECK(r256_11 == vector<uint8_t>({ 1, 10, 2, 4 }));
        //}
        {
            vector<uint8_t> input = { 1,43,54,7,8,9,7 };

            auto r58 = EncodeToBase58(input);
            WALLET_CHECK(r58 == "3ZzuVHW5C");

            auto r256 = DecodeBase58("3ZzuVHW5C");
            WALLET_CHECK(r256 == input);

            WALLET_CHECK(DecodeBase58("13ZzuVHW5C") == vector<uint8_t>({ 0, 1,43,54,7,8,9,7 }));
            WALLET_CHECK(DecodeBase58("C") == vector<uint8_t>{11});


            WALLET_CHECK(EncodeToBase58({}) == "");

            WALLET_CHECK(DecodeBase58("") == vector<uint8_t>{});

            WALLET_CHECK(DecodeBase58("3ZzuVHW5C==") == vector<uint8_t>{});

        }
        {
            vector<pair<string, string>> tests =
            {
                {"10c8511e", "Rt5zm"},
                {"00eb15231dfceb60925886b67d065299925915aeb172c06647", "1NS17iag9jJgTHD1VXjvLCEnZuQ3rJDE9L"},
                {"00000000000000000000", "1111111111"},
                {"", ""},
                {"61", "2g"},
                {"626262", "a3gV"},
                {"636363", "aPEr"},
                {"73696d706c792061206c6f6e6720737472696e67", "2cFupjhnEsSn59qHXstmK2ffpLv2"},
                {"516b6fcd0f", "ABnLTmg"},
                {"bf4f89001e670274dd", "3SEo3LWLoPntC"},
                {"572e4794", "3EFU7m"},
                {"ecac89cad93923c02321", "EJDM8drfXA6uyA"},
                {"000111d38e5fc9071ffcd20b4a763cc9ae4f252bb4e48fd66a835e252ada93ff480d6dd43dc62a641155a5", "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"},
                {"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9fa0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecfd0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", "1cWB5HCBdLjAuqGGReWE3R3CguuwSjw6RHn39s2yuDRTS5NsBgNiFpWgAnEx6VQi8csexkgYw3mdYrMHr8x9i7aEwP8kZ7vccXWqKDvGv3u1GxFKPuAkn8JCPPGDMf3vMMnbzm6Nh9zh1gcNsMvH3ZNLmP5fSG6DGbbi2tuwMWPthr4boWwCxf7ewSgNQeacyozhKDDQQ1qL5fQFUW52QKUZDZ5fw3KXNQJMcNTcaB723LchjeKun7MuGW5qyCBZYzA1KjofN1gYBV3NqyhQJ3Ns746GNuf9N2pQPmHz4xpnSrrfCvy6TVVz5d4PdrjeshsWQwpZsZGzvbdAdN8MKV5QsBDY"}
            };

            for (const auto& test : tests)
            {
                ByteBuffer value = from_hex(test.first);
                auto to = EncodeToBase58(value);
                auto buf = DecodeBase58(test.second);
                auto from = EncodeToBase58(buf);
                WALLET_CHECK(to == test.second);
                WALLET_CHECK(to == from);
            }
        }
    }

    void LogSqliteError(void* pArg, int iErrCode, const char* zMsg)
    {
        LOG_ERROR() << "(" << iErrCode << ") " << zMsg;
    }
}

bool RunNegLoop(beam::Negotiator::IBase& a, beam::Negotiator::IBase& b, const char* szTask)
{
	using namespace Negotiator;

	const uint32_t nPeers = 2;

	IBase* pArr[nPeers];
	pArr[0] = &a;
	pArr[1] = &b;

	for (uint32_t i = 0; i < nPeers; i++)
	{
		IBase& v = *pArr[i];
		v.Set(i, Codes::Role);
		v.Set(Rules::get().get_LastFork().m_Height, Codes::Scheme);
	}

	cout << "\nNegotiating: " << szTask << std::endl;

	bool pDone[nPeers] = { false };

	uint32_t nDone = 0;

	for (uint32_t i = 0; ; ++i %= nPeers)
	{
		if (pDone[i])
			continue;

		IBase& v = *pArr[i];

		Storage::Map gwOut;
		Gateway::Direct gw(gwOut);

		v.m_pGateway = &gw;

		uint32_t status = v.Update();

		char chThis = static_cast<char>('A' + i);

		if (!gwOut.empty())
		{
			char chOther = static_cast<char>('A' + !i);

			Gateway::Direct gwFin(*pArr[!i]->m_pStorage);

			size_t nSize = 0;
			for (Storage::Map::iterator it = gwOut.begin(); gwOut.end() != it; it++)
			{
				ByteBuffer& buf = it->second;
				uint32_t code = it->first;
				nSize += sizeof(code) + sizeof(uint32_t) + buf.size();
				gwFin.Send(code, std::move(buf));
			}

			cout << "\t" << chThis << " -> " << chOther << ' ' << nSize << " bytes" << std::endl;


			for (Storage::Map::iterator it = gwOut.begin(); gwOut.end() != it; it++)
			{
				uint32_t code = it->first;
				std::string sVar;
				v.QueryVar(sVar, code);

				if (sVar.empty())
					sVar = "?";
				cout << "\t     " << sVar << endl;
			}
		}

		if (!status)
			continue;

		if (Status::Success != status)
		{
			cout << "\t" << chThis << " Failed!" << std::endl;
			return false; // fail
		}

		pDone[i] = true;

		cout << "\t" << chThis << " done" << std::endl;
		if (++nDone == _countof(pArr))
			break;
	}

	return true;
}

Amount SetKidvs(beam::Negotiator::IBase& neg, const Amount* p, size_t n, uint32_t code, uint32_t i0 = 0)
{
	std::vector<Key::IDV> vec;
	vec.resize(n);
	Amount sum = 0;

	for (size_t i = 0; i < n; i++)
	{
		Key::IDV& kidv = vec[i];
		kidv = Zero;

		kidv.m_Type = Key::Type::Regular;
		kidv.m_Idx = i0 + static_cast<uint32_t>(i);

		kidv.m_Value = p[i];
		sum += p[i];
	}

	neg.Set(vec, code);
	return sum;
}

void TestNegotiation()
{
	using namespace Negotiator;

	cout << "TestNegotiation" << std::endl;

	const Amount valMSig = 11;
	Height hLock = 1440;

	WithdrawTx::CommonParam cpWd;
	cpWd.m_Krn2.m_pLock = &hLock;

	Multisig pT1[2];
	Storage::Map pS1[2];

	for (size_t i = 0; i < _countof(pT1); i++)
	{
		IBase& v = pT1[i];

		uintBig seed;
		ECC::GenRandom(seed);
		HKdf::Create(v.m_pKdf, seed);

		v.m_pStorage = pS1 + i;

		Key::IDV kidv(Zero);
		kidv.m_Value = valMSig;
		kidv.m_Idx = 500;
		kidv.m_Type = FOURCC_FROM(msg2);
		v.Set(kidv, Multisig::Codes::Kidv);

		v.Set(uint32_t(1), Multisig::Codes::ShareResult);
	}

	WALLET_CHECK(RunNegLoop(pT1[0], pT1[1], "MuSig"));


	MultiTx pT2[2];
	Storage::Map pS2[2];

	for (size_t i = 0; i < _countof(pT2); i++)
	{
		IBase& v = pT2[i];
		v.m_pKdf = pT1[i].m_pKdf;
		v.m_pStorage = pS2 + i;
	}

	const Amount pIn0[] = { 30, 50, 45 };
	const Amount pOut0[] = { 11, 12 };

	const Amount pIn1[] = { 6 };
	const Amount pOut1[] = { 17, 55 };

	Amount fee = valMSig;

	fee += SetKidvs(pT2[0], pIn0, _countof(pIn0), MultiTx::Codes::InpKidvs);
	fee -= SetKidvs(pT2[0], pOut0, _countof(pOut0), MultiTx::Codes::OutpKidvs, 700);

	fee += SetKidvs(pT2[1], pIn1, _countof(pIn1), MultiTx::Codes::InpKidvs);
	fee -= SetKidvs(pT2[1], pOut1, _countof(pOut1), MultiTx::Codes::OutpKidvs, 500);

	for (size_t i = 0; i < _countof(pT2); i++)
	{
		IBase& v = pT2[i];
		v.Set(fee, MultiTx::Codes::KrnFee);

		Key::IDV kidv(Zero);
		kidv.m_Value = valMSig;
		kidv.m_Idx = 500;
		kidv.m_Type = FOURCC_FROM(msg2);
		v.Set(kidv, MultiTx::Codes::InpMsKidv);

		uint32_t idxTrg = MultiTx::Codes::InpMsCommitment;
		uint32_t idxSrc = Multisig::Codes::Commitment;

		pS2[i][idxTrg] = pS1[i][idxSrc];
	}

	WALLET_CHECK(RunNegLoop(pT2[0], pT2[1], "Transaction-with-MuSig"));

	WithdrawTx pT3[2];
	Storage::Map pS3[2];

	for (size_t i = 0; i < _countof(pT3); i++)
	{
		WithdrawTx& v = pT3[i];
		v.m_pKdf = pT1[i].m_pKdf;
		v.m_pStorage = pS3 + i;

		WithdrawTx::Worker wrk(v);

		// new multisig
		Key::IDV ms0(Zero);
		ms0.m_Value = valMSig;
		ms0.m_Idx = 500;
		ms0.m_Type = FOURCC_FROM(msg2);

		Key::IDV ms1 = ms0;
		ms1.m_Idx = 800;

		ECC::Point comm0;
		WALLET_CHECK(pT1[i].Get(comm0, Multisig::Codes::Commitment));


		std::vector<Key::IDV> vec;
		vec.resize(1, Zero);
		vec[0].m_Idx = 315;
		vec[0].m_Type = Key::Type::Regular;

		Amount half = valMSig / 2;
		vec[0].m_Value = i ? half : (valMSig - half);

		v.Setup(true, &ms1, &ms0, &comm0, &vec, cpWd);
	}

	WALLET_CHECK(RunNegLoop(pT3[0], pT3[1], "Withdraw-Tx ritual"));


	struct ChannelData
	{
		Key::IDV m_msMy;
		Key::IDV m_msPeer;
		ECC::Point m_CommPeer;
	};

	ChannelOpen pT4[2];
	Storage::Map pS4[2];
	ChannelData pCData[2];

	for (size_t i = 0; i < _countof(pT4); i++)
	{
		ChannelOpen& v = pT4[i];
		v.m_pKdf = pT1[i].m_pKdf;
		v.m_pStorage = pS4 + i;

		ChannelOpen::Worker wrk(v);

		Amount half = valMSig / 2;
		Amount nMyValue = i ? half : (valMSig - half);

		std::vector<Key::IDV> vIn, vOutWd;
		vIn.resize(1, Zero);
		vIn[0].m_Idx = 215;
		vIn[0].m_Type = Key::Type::Regular;
		vIn[0].m_Value = nMyValue;

		vOutWd.resize(1, Zero);
		vOutWd[0].m_Idx = 216;
		vOutWd[0].m_Type = Key::Type::Regular;
		vOutWd[0].m_Value = nMyValue;

		Key::IDV ms0(Zero);
		ms0.m_Value = valMSig;
		ms0.m_Type = FOURCC_FROM(msg2);
		ms0.m_Idx = 220;

		Key::IDV msA = ms0;
		msA.m_Idx++;
		Key::IDV msB = msA;
		msB.m_Idx++;

		v.Setup(true, &vIn, nullptr, &ms0, MultiTx::KernelParam(), &msA, &msB, &vOutWd, cpWd);

		ChannelData& cd = pCData[i];
		cd.m_msMy = i ? msB : msA;
		cd.m_msPeer = i ? msA : msB;
	}

	WALLET_CHECK(RunNegLoop(pT4[0], pT4[1], "Lightning channel open"));

	for (int i = 0; i < 2; i++)
	{
		ChannelOpen::Result r;
		ChannelOpen::Worker wrk(pT4[i]);
		pT4[i].get_Result(r);

		WALLET_CHECK(!r.m_tx1.m_vKernels.empty());
		WALLET_CHECK(!r.m_tx2.m_vKernels.empty());
		WALLET_CHECK(!r.m_txPeer2.m_vKernels.empty());

		pCData[i].m_CommPeer = r.m_CommPeer1;
	}


	ChannelUpdate pT5[2];
	Storage::Map pS5[2];


	for (size_t i = 0; i < _countof(pT5); i++)
	{
		ChannelUpdate& v = pT5[i];
		v.m_pKdf = pT1[i].m_pKdf;
		v.m_pStorage = pS5 + i;

		ChannelUpdate::Worker wrk(v);

		Amount nPart = valMSig / 3;
		Amount nMyValue = i ? nPart : (valMSig - nPart);

		Key::IDV ms0;
		ECC::Point comm0;
		{
			ChannelOpen::Worker wrk2(pT4[i]);
			pT4[i].m_MSig.Get(comm0, Multisig::Codes::Commitment);
			pT4[i].m_MSig.Get(ms0, Multisig::Codes::Kidv);
		}

		Key::IDV msA = ms0;
		msA.m_Idx += 15;
		Key::IDV msB = msA;
		msB.m_Idx++;

		std::vector<Key::IDV> vOutWd;
		vOutWd.resize(1, Zero);
		vOutWd[0].m_Idx = 216;
		vOutWd[0].m_Type = Key::Type::Regular;
		vOutWd[0].m_Value = nMyValue;

		ChannelData& cd = pCData[i];

		v.Setup(true, &ms0, &comm0, &msA, &msB, &vOutWd, cpWd, &cd.m_msMy, &cd.m_msPeer, &cd.m_CommPeer);
	}

	WALLET_CHECK(RunNegLoop(pT5[0], pT5[1], "Lightning channel update"));

	for (int i = 0; i < 2; i++)
	{
		ChannelUpdate::Result r;
		ChannelUpdate::Worker wrk(pT5[i]);
		pT5[i].get_Result(r);

		WALLET_CHECK(!r.m_tx1.m_vKernels.empty());
		WALLET_CHECK(!r.m_tx2.m_vKernels.empty());
		WALLET_CHECK(!r.m_txPeer2.m_vKernels.empty());

		WALLET_CHECK(r.m_RevealedSelfKey && r.m_PeerKeyValid);
	}
}


#if defined(BEAM_HW_WALLET)

IWalletDB::Ptr createSqliteWalletDB()
{
    const char* dbName = "wallet.db";
    if (boost::filesystem::exists(dbName))
    {
        boost::filesystem::remove(dbName);
    }
    ECC::NoLeak<ECC::uintBig> seed;
    seed.V = Zero;
    auto walletDB = WalletDB::init(dbName, string("pass123"), seed, io::Reactor::get_Current().shared_from_this());
    beam::Block::SystemState::ID id = { };
    id.m_Height = 134;
    walletDB->setSystemStateID(id);
    return walletDB;
}

void TestHWTransaction(IPrivateKeyKeeper& pkk)
{
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    Point::Native totalPublicExcess = Zero;

    std::vector<Coin::ID> inputCoins =
    {
        {40, 0, Key::Type::Regular},
    };

    std::vector<Coin::ID> outputCoins =
    {
        {16, 0, Key::Type::Regular},
        {24, 0, Key::Type::Regular},
    };

    beam::Amount fee = 0;
    ECC::Scalar::Native offset = Zero;
    offset.GenRandomNnz();

    {

        Point::Native publicAmount = Zero;
        Amount amount = 0;
        for (const auto& cid : inputCoins)
        {
            amount += cid.m_Value;
        }
        AmountBig::AddTo(publicAmount, amount);
        amount = 0;
        publicAmount = -publicAmount;
        for (const auto& cid : outputCoins)
        {
            amount += cid.m_Value;
        }
        AmountBig::AddTo(publicAmount, amount);

        Point::Native publicExcess = Context::get().G * offset;

        {
            Point::Native commitment;

            for (const auto& output : outputCoins)
            {
                if (commitment.Import(pkk.GeneratePublicKeySync(output, true)))
                {
                    publicExcess += commitment;
                }
            }

            publicExcess = -publicExcess;
            for (const auto& input : inputCoins)
            {
                if (commitment.Import(pkk.GeneratePublicKeySync(input, true)))
                {
                    publicExcess += commitment;
                }
            }
        }

        publicExcess += publicAmount;

        totalPublicExcess = publicExcess;
    }

    {
        ECC::Point::Native peerPublicNonce = Zero;

        TxKernel kernel;
        kernel.m_Fee = fee;
        kernel.m_Height = { 25000, 27500 };
        kernel.m_Commitment = totalPublicExcess;

        ECC::Hash::Value message;
        kernel.get_Hash(message);

        KernelParameters kernelParameters;
        kernelParameters.fee = fee;

        kernelParameters.height = { 25000, 27500 };
        kernelParameters.commitment = totalPublicExcess;

        Signature signature;

        ECC::Point::Native publicNonce;
        uint8_t nonceSlot = (uint8_t)pkk.AllocateNonceSlot();
        publicNonce.Import(pkk.GenerateNonceSync(nonceSlot));


        signature.m_NoncePub = publicNonce + peerPublicNonce;
        signature.m_k = pkk.SignSync(inputCoins, outputCoins, offset, nonceSlot, kernelParameters, publicNonce + peerPublicNonce);

        if (signature.IsValid(message, totalPublicExcess))
        {
            LOG_DEBUG() << "Ok, signature is valid :)";
        }
        else
        {
            LOG_ERROR() << "Error, invalid signature :(";
        }
    }
}

#include "mnemonic/mnemonic.h"

void TestHWCommitment()
{
    cout << "Test HW commitment" << std::endl;

    Key::IDV kidv;
    kidv.m_Value = 11100000000;
    kidv.m_Idx = 1887367845482021531;
    kidv.m_Type = 1852797549;
    kidv.m_SubIdx = 16777216;

    Point comm1, comm2;
    {
        Scalar::Native secretKey;

        //beam::WordList generatedPhrases = {"budget", "focus", "surface", "plug", "dragon", "elephant", "token", "child", "kitchen", "coast", "lounge", "mean" };
        beam::WordList generatedPhrases = { "copy", "vendor", "shallow", "raven", "coffee", "appear", "book", "blast", "lock", "exchange", "farm", "glue" };
        
        auto buf = beam::decodeMnemonic(generatedPhrases);

        SecString secretSeed;
        secretSeed.assign(buf.data(), buf.size());

        Key::IKdf::Ptr kdf;
        ECC::HKdf::Create(kdf, secretSeed.hash().V);

        SwitchCommitment().Create(secretKey, comm1, *MasterKey::get_Child(kdf, kidv), kidv);

        LOG_INFO() << "commitment is " << comm1;
    }

    {
        HWWallet hw;

        comm2 = hw.generateKeySync(kidv, true);

        LOG_INFO() << "HW commitment is " << comm2;
    }

    WALLET_CHECK(comm1 == comm2);
}

void TestHWWallet()
{
    cout << "Test HW wallet" << std::endl;

    HWWallet hw;
    //hw.getOwnerKey([](const std::string& key)
    //{
    //    LOG_INFO() << "HWWallet.getOwnerKey(): " << key;
    //});

    //hw.generateNonce(1, [](const ECC::Point& nonce)
    //{
    //    LOG_INFO() << "HWWallet.generateNonce(): " << nonce;
    //});

    const ECC::Key::IDV kidv(100500, 15, Key::Type::Regular, 7, ECC::Key::IDV::Scheme::V0);

    ECC::Point pt2 = hw.generateKeySync(kidv, true);

    {
        // Recovery seed: copy, vendor, shallow, raven, coffee, appear, book, blast, lock, exchange, farm, glue
        uint8_t x[] = {0xce, 0xb2, 0x0d, 0xa2, 0x73, 0x07, 0x0e, 0xb9, 0xc8, 0x2e, 0x47, 0x5b, 0x6f, 0xa0, 0x7b, 0x85, 0x8d, 0x2c, 0x40, 0x9b, 0x9c, 0x24, 0x31, 0xba, 0x3a, 0x8e, 0x2c, 0xba, 0x7b, 0xa1, 0xb0, 0x04};
        ECC::Point pt;
        pt.m_X = beam::Blob(x, 32);
        pt.m_Y = 1;
        WALLET_CHECK(pt == pt2);
    }

    hw.generateRangeProof(kidv, false, [&pt2](const ECC::RangeProof::Confidential &rp) {
        auto hGen = beam::SwitchCommitment(NULL).m_hGen;


        ECC::Point::Native comm;
        comm.Import(pt2);
        {
            Oracle oracle;
            oracle << 0u;
            oracle << pt2;
            LOG_INFO() << "rp.IsValid(): " << rp.IsValid(comm, oracle, &hGen);
        }

        {
            Oracle oracle;
            oracle << 0u;
            oracle << pt2;
            WALLET_CHECK(rp.IsValid(comm, oracle, &hGen));
        }
    });

    {
        Height scheme = 100500;
        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);
        TrezorKeyKeeper tk;
        auto db = createSqliteWalletDB();
        LocalPrivateKeyKeeper lpkk(db, db->get_MasterKdf());
        IPrivateKeyKeeper& pkk = tk;
        ECC::Point::Native comm2;
        auto outputs = pkk.GenerateOutputsSync(scheme, { kidv });
        WALLET_CHECK(outputs[0]->IsValid(scheme, comm2));
    }

    // test transaction sign with local key keeper
    {
        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);

        auto db = createSqliteWalletDB();
        LocalPrivateKeyKeeper lpkk(db, db->get_MasterKdf());
        TestHWTransaction(lpkk);
    }

    // test transaction sign with HW key keeper
    {
        TrezorKeyKeeper trezor;
        TestHWTransaction(trezor);
    }

}
#endif

int main()
{
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    const auto path = boost::filesystem::system_complete("logs");
    auto logger = beam::Logger::create(logLevel, logLevel, logLevel, "wallet_test", path.string());


    Rules::get().FakePoW = true;
	Rules::get().pForks[1].m_Height = 100500; // needed for lightning network to work
    //Rules::get().DA.MaxAhead_s = 90;// 60 * 1;
    Rules::get().UpdateChecksum();

    sqlite3_config(SQLITE_CONFIG_LOG, LogSqliteError, nullptr);

    TestConvertions();
    TestTxParameters();
   
	TestNegotiation();
   
    TestP2PWalletNegotiationST();

    TestTxRollback();
   
    {
        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);
        //TestWalletNegotiation(CreateWalletDB<TestWalletDB>(), CreateWalletDB<TestWalletDB2>());
        TestWalletNegotiation(createSenderWalletDB(), createReceiverWalletDB());
    }
   
    TestSplitTransaction();
   
    TestMinimalFeeTransaction();
   
    TestTxToHimself();
   
    TestExpiredTransaction();
   
    TestTransactionUpdate();
    //TestTxPerformance();
    //TestTxNonces();
   
    TestColdWalletSending();
    TestColdWalletReceiving();
   
    TestTxExceptionHandling();
   
    // @nesbox: disabled tests, they work only if device connected
//#if defined(BEAM_HW_WALLET)
//    TestHWCommitment();
//    TestHWWallet();
//#endif

    //TestBbsMessages();
    //TestBbsMessages2();

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}
