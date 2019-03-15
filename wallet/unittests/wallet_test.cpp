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
#include "utility/test_helpers.h"
#include "../../core/radixtree.h"
#include "../../core/unittest/mini_blockchain.h"
#include <string_view>
#include "wallet/wallet_transaction.h"

#include "test_helpers.h"

#include <assert.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

#include "core/proto.h"
#include <boost/filesystem.hpp>
#include <boost/intrusive/list.hpp>

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

        WalletAddress wa = wallet::createAddress(*receiverWalletDB);
        receiverWalletDB->saveAddress(wa);
        WalletID receiver_id = wa.m_walletID;

        wa = wallet::createAddress(*senderWalletDB);
        senderWalletDB->saveAddress(wa);
        WalletID sender_id = wa.m_walletID;

        int count = 0;
        auto f = [&count](const auto& /*id*/)
        {
            if (++count >= 2)
                io::Reactor::get_Current().stop();
        };

        Wallet sender(senderWalletDB, f);
        Wallet receiver(receiverWalletDB, f);

        TestWalletNetwork twn;
        TestNodeNetwork::Shared tnns;
        TestNodeNetwork netNodeS(tnns, sender), netNodeR(tnns, receiver);

        sender.set_Network(netNodeS, twn);
        receiver.set_Network(netNodeR, twn);

        twn.m_Map[sender_id].m_pSink = &sender;
        twn.m_Map[receiver_id].m_pSink = &receiver;

        tnns.AddBlock();

        sender.transfer_money(sender_id, receiver_id, 6, 1, true, 200, {});
        mainReactor->run();

        WALLET_CHECK(count == 2);
    }

    void TestTxToHimself()
    {
        cout << "\nTesting Tx to himself...\n";

        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);

        auto senderWalletDB = createSqliteWalletDB("sender_wallet.db");

        // add coin with keyType - Coinbase
        beam::Amount coin_amount = 40;
        Coin coin = CreateAvailCoin(coin_amount, 0);
        coin.m_ID.m_Type = Key::Type::Coinbase;
        senderWalletDB->store(coin);

        auto coins = senderWalletDB->selectCoins(24);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_ID.m_Type == Key::Type::Coinbase);
        WALLET_CHECK(coins[0].m_status == Coin::Available);
        WALLET_CHECK(senderWalletDB->getTxHistory().empty());

        TestNode node;
        TestWalletRig sender("sender", senderWalletDB, [](auto) { io::Reactor::get_Current().stop(); });
        helpers::StopWatch sw;

        sw.start();
        auto txId = sender.m_Wallet.transfer_money(sender.m_WalletID, sender.m_WalletID, 24, 2, true, 200);
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
        senderWalletDB->visit([&newSenderCoins](const Coin& c)->bool
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
        TestWalletRig sender("sender", createSenderWalletDB(), f);
        TestWalletRig receiver("receiver", createReceiverWalletDB(), f);

        WALLET_CHECK(sender.m_WalletDB->selectCoins(6).size() == 2);
        WALLET_CHECK(sender.m_WalletDB->getTxHistory().empty());
        WALLET_CHECK(receiver.m_WalletDB->getTxHistory().empty());

        helpers::StopWatch sw;
        sw.start();

        auto txId = sender.m_Wallet.transfer_money(sender.m_WalletID, receiver.m_WalletID, 4, 2, true, 200);

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

        // second transfer
        auto preselectedCoins = sender.m_WalletDB->selectCoins(6);
        CoinIDList preselectedIDs;
        for (const auto& c : preselectedCoins)
        {
            preselectedIDs.push_back(c.m_ID);
        }
        sw.start();
        txId = sender.m_Wallet.transfer_money(sender.m_WalletID, receiver.m_WalletID, 6, 0, preselectedIDs, true, 200);
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
        txId = sender.m_Wallet.transfer_money(sender.m_WalletID, receiver.m_WalletID, 6, 0, true, 200);
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

    void TestP2PWalletReverseNegotiationST()
    {
        cout << "\nTesting p2p wallets negotiation (reverse version)...\n";

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
        TestWalletRig receiver("receiver", createReceiverWalletDB(), f);
  
        WALLET_CHECK(sender.m_WalletDB->selectCoins(6).size() == 2);
        WALLET_CHECK(sender.m_WalletDB->getTxHistory().empty());
        WALLET_CHECK(receiver.m_WalletDB->getTxHistory().empty());

        helpers::StopWatch sw;
        sw.start();

        auto txId = receiver.m_Wallet.transfer_money(receiver.m_WalletID, sender.m_WalletID, 4, 2, false, 200);

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
        WALLET_CHECK(stx->m_createTime >= rtx->m_createTime);
        WALLET_CHECK(stx->m_status == rtx->m_status);
        WALLET_CHECK(stx->m_sender == true);
        WALLET_CHECK(rtx->m_sender == false);

        // second transfer
        sw.start();
        txId = receiver.m_Wallet.transfer_money(receiver.m_WalletID, sender.m_WalletID, 6, 0, false, 200);
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


        WALLET_CHECK(newSenderCoins[0].m_ID.m_Value == 3);
        WALLET_CHECK(newSenderCoins[0].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[0].m_ID.m_Type == Key::Type::Change);

        WALLET_CHECK(newSenderCoins[1].m_ID.m_Value == 5);
        WALLET_CHECK(newSenderCoins[1].m_status == Coin::Spent);
        WALLET_CHECK(newSenderCoins[1].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newSenderCoins[2].m_ID.m_Value == 2);
        WALLET_CHECK(newSenderCoins[2].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[2].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newSenderCoins[3].m_ID.m_Value == 1);
        WALLET_CHECK(newSenderCoins[3].m_status == Coin::Spent);
        WALLET_CHECK(newSenderCoins[3].m_ID.m_Type == Key::Type::Regular);

        WALLET_CHECK(newSenderCoins[4].m_ID.m_Value == 9);
        WALLET_CHECK(newSenderCoins[4].m_status == Coin::Spent);
        WALLET_CHECK(newSenderCoins[4].m_ID.m_Type == Key::Type::Regular);

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
        WALLET_CHECK(stx->m_createTime >= rtx->m_createTime);
        WALLET_CHECK(stx->m_status == rtx->m_status);
        WALLET_CHECK(stx->m_sender == true);
        WALLET_CHECK(rtx->m_sender == false);


        // third transfer. no enough money should appear
        sw.start();

        txId = receiver.m_Wallet.transfer_money(receiver.m_WalletID, sender.m_WalletID, 6, 0, false, 200);
        mainReactor->run();
        sw.stop();
        cout << "Third transfer elapsed time: " << sw.milliseconds() << " ms\n";
        // check coins
        newSenderCoins = sender.GetCoins();
        newReceiverCoins = receiver.GetCoins();

        // no coins 
        WALLET_CHECK(newSenderCoins.size() == 5);
        WALLET_CHECK(newReceiverCoins.size() == 2);

        // Tx history check. New failed tx should be added to sender and receiver
        sh = sender.m_WalletDB->getTxHistory();
        WALLET_CHECK(sh.size() == 3);
        rh = receiver.m_WalletDB->getTxHistory();
        WALLET_CHECK(rh.size() == 3);
        stx = sender.m_WalletDB->getTx(txId);
        WALLET_CHECK(stx.is_initialized());
        rtx = receiver.m_WalletDB->getTx(txId);
        WALLET_CHECK(rtx.is_initialized());

        WALLET_CHECK(rtx->m_amount == 6);
        WALLET_CHECK(rtx->m_status == TxStatus::Failed);
        WALLET_CHECK(rtx->m_sender == false);


        WALLET_CHECK(stx->m_amount == 6);
        WALLET_CHECK(stx->m_status == TxStatus::Failed);
        WALLET_CHECK(stx->m_sender == true);
    }

    void TestSwapTransaction()
    {
        cout << "\nTesting atomic swap transaction...\n";

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
        TestWalletRig receiver("receiver", createReceiverWalletDB(), f);

        /*TxID txID =*/ sender.m_Wallet.swap_coins(sender.m_WalletID, receiver.m_WalletID, 4, 1, wallet::AtomicSwapCoin::Bitcoin, 2);

        auto receiverCoins = receiver.GetCoins();
        WALLET_CHECK(receiverCoins.empty());

        mainReactor->run();

        receiverCoins = receiver.GetCoins();
        WALLET_CHECK(receiverCoins.size() == 1);
        WALLET_CHECK(receiverCoins[0].m_ID.m_Value == 4);
    }

    void TestSplitTransaction()
    {
        cout << "\nTesting split Tx...\n";

        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);

        auto senderWalletDB = createSqliteWalletDB("sender_wallet.db");

        // add coin with keyType - Coinbase
        beam::Amount coin_amount = 40;
        Coin coin = CreateAvailCoin(coin_amount, 0);
        coin.m_ID.m_Type = Key::Type::Coinbase;
        senderWalletDB->store(coin);

        auto coins = senderWalletDB->selectCoins(24);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_ID.m_Type == Key::Type::Coinbase);
        WALLET_CHECK(coins[0].m_status == Coin::Available);
        WALLET_CHECK(senderWalletDB->getTxHistory().empty());

        TestNode node;
        TestWalletRig sender("sender", senderWalletDB, [](auto) { io::Reactor::get_Current().stop(); });
        helpers::StopWatch sw;

        sw.start();
        auto txId = sender.m_Wallet.split_coins(sender.m_WalletID, { 11, 12, 13 }, 2, true, 200);
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
        senderWalletDB->visit([&newSenderCoins](const Coin& c)->bool
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

    void TestExpiredTransaction()
    {
        cout << "\nTesting expited Tx...\n";

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
        TestWalletRig receiver("receiver", createReceiverWalletDB(), f);

        WALLET_CHECK(sender.m_WalletDB->selectCoins(6).size() == 2);
        WALLET_CHECK(sender.m_WalletDB->getTxHistory().empty());
        WALLET_CHECK(receiver.m_WalletDB->getTxHistory().empty());

        auto txId = sender.m_Wallet.transfer_money(sender.m_WalletID, receiver.m_WalletID, 4, 2, true);

        mainReactor->run();

        // check coins
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

        txId = sender.m_Wallet.transfer_money(sender.m_WalletID, receiver.m_WalletID, 4, 2, true);

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
        struct TestGateway : wallet::INegotiatorGateway
        {
            void on_tx_completed(const TxID&) override {}
            void register_tx(const TxID&, Transaction::Ptr) override  {}
            void confirm_outputs(const std::vector<Coin>&) override  {}
            void confirm_kernel(const TxID&, const TxKernel&) override  {}
            void confirm_kernel(const TxID&, const Merkle::Hash&) override {}
            void get_kernel(const TxID& txID, const Merkle::Hash& kernelID) override {}
            bool get_tip(Block::SystemState::Full& state) const override { return false; }
            void send_tx_params(const WalletID& peerID, wallet::SetTxParameter&&) override {}
            BitcoinRPC::Ptr get_bitcoin_rpc() const override { return nullptr; }
        } gateway;
        TestWalletRig sender("sender", createSenderWalletDB());
        TestWalletRig receiver("receiver", createReceiverWalletDB());

        TxID txID = wallet::GenerateTxID();
        auto tx = make_shared<wallet::SimpleTransaction>(gateway, sender.m_WalletDB, txID);
        Height currentHeight = sender.m_WalletDB->getCurrentHeight();

        tx->SetParameter(wallet::TxParameterID::TransactionType, wallet::TxType::Simple, false);
        tx->SetParameter(wallet::TxParameterID::MaxHeight, currentHeight + 2, false); // transaction is valid +lifetime blocks from currentHeight
        tx->SetParameter(wallet::TxParameterID::IsInitiator, true, false);
        //tx->SetParameter(wallet::TxParameterID::AmountList, {1U}, false);
      //  tx->SetParameter(wallet::TxParameterID::PreselectedCoins, {}, false);

        TxDescription txDescription;

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

    void TestTxPerformance()
    {
        cout << "\nTesting tx performance...\n";

        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);

        const int TxCount = 5;

        int completedCount = 2 * TxCount;
        auto f = [&completedCount, mainReactor, count = 2 * TxCount](auto)
        {
            --completedCount;
            if (completedCount == 0)
            {
                mainReactor->stop();
                completedCount = count;
            }
        };

        TestNode node;
        TestWalletRig sender("sender", createSenderWalletDB(TxCount, 6), f);
        TestWalletRig receiver("receiver", createReceiverWalletDB(), f);

        helpers::StopWatch sw;
        sw.start();

        for (int i = 0; i < TxCount; ++i)
        {
            sender.m_Wallet.transfer_money(sender.m_WalletID, receiver.m_WalletID, 5, 1, true, 200);
        }
        
        mainReactor->run();
        sw.stop();
        cout << "Transferring of " << TxCount << " transactions took :" << sw.milliseconds() << " ms\n";
    }
}

int main()
{
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = beam::Logger::create(logLevel, logLevel);

    Rules::get().FakePoW = true;
    Rules::get().UpdateChecksum();

    TestP2PWalletNegotiationST();
    //TestP2PWalletReverseNegotiationST();

    //TestWalletNegotiation(CreateWalletDB<TestWalletDB>(), CreateWalletDB<TestWalletDB2>());
    TestWalletNegotiation(createSenderWalletDB(), createReceiverWalletDB());

    TestSplitTransaction();

    ////TestSwapTransaction();

    TestTxToHimself();

    TestExpiredTransaction();

    TestTransactionUpdate();
    //TestTxPerformance();

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}
