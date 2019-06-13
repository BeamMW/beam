// Copyright 2019 The Beam Team
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

#include "wallet/common.h"
#include "wallet/wallet_network.h"
#include "wallet/wallet.h"
#include "wallet/secstring.h"
#include "wallet/bitcoin/options.h"
#include "wallet/litecoin/options.h"
#include "wallet/qtum/options.h"
#include "utility/test_helpers.h"
#include "../../core/radixtree.h"
#include "../../core/unittest/mini_blockchain.h"
#include <string_view>
#include "wallet/wallet_transaction.h"
#include "../../core/negotiator.h"

#include "test_helpers.h"

#include <boost/filesystem.hpp>
#include <boost/intrusive/list.hpp>

using namespace beam;
using namespace std;
using namespace ECC;

WALLET_TEST_INIT

#include "wallet_test_environment.cpp"
#include "swap_test_environment.cpp"

void TestSwapTransaction(bool isBeamOwnerStart)
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

    io::Address senderAddress;
    senderAddress.resolve("127.0.0.1:10400");

    io::Address receiverAddress;
    receiverAddress.resolve("127.0.0.1:10300");

    Amount beamAmount = 3;
    Amount beamFee = 1;
    Amount swapAmount = 2000;
    Amount feeRate = 256;

    BitcoinOptions bobOptions;
    bobOptions.m_userName = "Bob";
    bobOptions.m_pass = "123";
    bobOptions.m_address = senderAddress;
    bobOptions.m_feeRate = feeRate;

    BitcoinOptions aliceOptions;
    aliceOptions.m_userName = "Alice";
    aliceOptions.m_pass = "123";
    aliceOptions.m_address = receiverAddress;
    aliceOptions.m_feeRate = feeRate;

    sender.m_Wallet.initBitcoin(*mainReactor, bobOptions);
    receiver.m_Wallet.initBitcoin(*mainReactor, aliceOptions);

    TestBitcoinWallet::Options senderOptions;
    senderOptions.m_rawAddress = "2N8N2kr34rcGqHCo3aN6yqniid8a4Mt3FCv";
    senderOptions.m_privateKey = "cSFMca7FAeAgLRgvev5ajC1v1jzprBr1KoefUFFPS8aw3EYwLArM";
    senderOptions.m_refundTx = "0200000001809fc0890cb2724a941dfc3b7213a63b3017b0cddbed4f303be300cb55ddca830100000000ffffffff01e8030000000000001976a9146ed612a79317bc6ade234f299073b945ccb3e76b88ac00000000";
    senderOptions.m_amount = swapAmount;

    TestBitcoinWallet senderBtcWallet(*mainReactor, senderAddress, senderOptions);
    TestBitcoinWallet::Options receiverOptions;
    receiverOptions.m_rawAddress = "2Mvfsv3JiwWXjjwNZD6LQJD4U4zaPAhSyNB";
    receiverOptions.m_privateKey = "cNoRPsNczFw6b7wTuwLx24gSnCPyF3CbvgVmFJYKyfe63nBsGFxr";
    receiverOptions.m_refundTx = "0200000001809fc0890cb2724a941dfc3b7213a63b3017b0cddbed4f303be300cb55ddca830100000000ffffffff01e8030000000000001976a9146ed612a79317bc6ade234f299073b945ccb3e76b88ac00000000";
    receiverOptions.m_amount = swapAmount;

    TestBitcoinWallet receiverBtcWallet(*mainReactor, receiverAddress, receiverOptions);

    receiverBtcWallet.addPeer(senderAddress);
    TxID txID = { {0} };

    if (isBeamOwnerStart)
    {
        receiver.m_Wallet.initSwapConditions(beamAmount, swapAmount, wallet::AtomicSwapCoin::Bitcoin, false);
        txID = sender.m_Wallet.swap_coins(sender.m_WalletID, receiver.m_WalletID, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, true);
    }
    else
    {
        sender.m_Wallet.initSwapConditions(beamAmount, swapAmount, wallet::AtomicSwapCoin::Bitcoin, true);
        txID = receiver.m_Wallet.swap_coins(receiver.m_WalletID, sender.m_WalletID, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, false);
    }

    auto receiverCoins = receiver.GetCoins();
    WALLET_CHECK(receiverCoins.empty());

    io::Timer::Ptr timer = io::Timer::create(*mainReactor);
    timer->start(5000, true, [&node]() {node.AddBlock(); });

    mainReactor->run();

    receiverCoins = receiver.GetCoins();
    WALLET_CHECK(receiverCoins.size() == 1);
    WALLET_CHECK(receiverCoins[0].m_ID.m_Value == beamAmount);
    WALLET_CHECK(receiverCoins[0].m_status == Coin::Available);
    WALLET_CHECK(receiverCoins[0].m_createTxId == txID);

    auto senderCoins = sender.GetCoins();
    WALLET_CHECK(senderCoins.size() == 5);
    WALLET_CHECK(senderCoins[0].m_ID.m_Value == 5);
    WALLET_CHECK(senderCoins[0].m_status == Coin::Spent);
    WALLET_CHECK(senderCoins[0].m_spentTxId == txID);
    // change
    WALLET_CHECK(senderCoins[4].m_ID.m_Value == 1);
    WALLET_CHECK(senderCoins[4].m_status == Coin::Available);
    WALLET_CHECK(senderCoins[4].m_createTxId == txID);
}

void TestSwapTransactionWithoutChange(bool isBeamOwnerStart)
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

    io::Address senderAddress;
    senderAddress.resolve("127.0.0.1:10400");

    io::Address receiverAddress;
    receiverAddress.resolve("127.0.0.1:10300");

    Amount beamAmount = 4;
    Amount beamFee = 1;
    Amount swapAmount = 2000;
    Amount feeRate = 256;

    BitcoinOptions bobOptions;
    bobOptions.m_userName = "Bob";
    bobOptions.m_pass = "123";
    bobOptions.m_address = senderAddress;
    bobOptions.m_feeRate = feeRate;

    BitcoinOptions aliceOptions;
    aliceOptions.m_userName = "Alice";
    aliceOptions.m_pass = "123";
    aliceOptions.m_address = receiverAddress;
    aliceOptions.m_feeRate = feeRate;

    sender.m_Wallet.initBitcoin(*mainReactor, bobOptions);
    receiver.m_Wallet.initBitcoin(*mainReactor, aliceOptions);

    TestBitcoinWallet::Options senderOptions;
    senderOptions.m_rawAddress = "2N8N2kr34rcGqHCo3aN6yqniid8a4Mt3FCv";
    senderOptions.m_privateKey = "cSFMca7FAeAgLRgvev5ajC1v1jzprBr1KoefUFFPS8aw3EYwLArM";
    senderOptions.m_refundTx = "0200000001809fc0890cb2724a941dfc3b7213a63b3017b0cddbed4f303be300cb55ddca830100000000ffffffff01e8030000000000001976a9146ed612a79317bc6ade234f299073b945ccb3e76b88ac00000000";
    senderOptions.m_amount = swapAmount;

    TestBitcoinWallet senderBtcWallet(*mainReactor, senderAddress, senderOptions);
    TestBitcoinWallet::Options receiverOptions;
    receiverOptions.m_rawAddress = "2Mvfsv3JiwWXjjwNZD6LQJD4U4zaPAhSyNB";
    receiverOptions.m_privateKey = "cNoRPsNczFw6b7wTuwLx24gSnCPyF3CbvgVmFJYKyfe63nBsGFxr";
    receiverOptions.m_refundTx = "0200000001809fc0890cb2724a941dfc3b7213a63b3017b0cddbed4f303be300cb55ddca830100000000ffffffff01e8030000000000001976a9146ed612a79317bc6ade234f299073b945ccb3e76b88ac00000000";
    receiverOptions.m_amount = swapAmount;

    TestBitcoinWallet receiverBtcWallet(*mainReactor, receiverAddress, receiverOptions);

    receiverBtcWallet.addPeer(senderAddress);
    TxID txID = { {0} };

    if (isBeamOwnerStart)
    {
        receiver.m_Wallet.initSwapConditions(beamAmount, swapAmount, wallet::AtomicSwapCoin::Bitcoin, false);
        txID = sender.m_Wallet.swap_coins(sender.m_WalletID, receiver.m_WalletID, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, true);
    }
    else
    {
        sender.m_Wallet.initSwapConditions(beamAmount, swapAmount, wallet::AtomicSwapCoin::Bitcoin, true);
        txID = receiver.m_Wallet.swap_coins(receiver.m_WalletID, sender.m_WalletID, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, false);
    }

    auto receiverCoins = receiver.GetCoins();
    WALLET_CHECK(receiverCoins.empty());

    io::Timer::Ptr timer = io::Timer::create(*mainReactor);
    timer->start(5000, true, [&node]() {node.AddBlock(); });

    mainReactor->run();

    receiverCoins = receiver.GetCoins();
    WALLET_CHECK(receiverCoins.size() == 1);
    WALLET_CHECK(receiverCoins[0].m_ID.m_Value == beamAmount);
    WALLET_CHECK(receiverCoins[0].m_status == Coin::Available);
    WALLET_CHECK(receiverCoins[0].m_createTxId == txID);

    auto senderCoins = sender.GetCoins();
    WALLET_CHECK(senderCoins.size() == 4);
    WALLET_CHECK(senderCoins[0].m_ID.m_Value == 5);
    WALLET_CHECK(senderCoins[0].m_status == Coin::Spent);
    WALLET_CHECK(senderCoins[0].m_spentTxId == txID);
}

int main()
{
    int logLevel = LOG_LEVEL_DEBUG;
    auto logger = beam::Logger::create(logLevel, logLevel);
    Rules::get().FakePoW = true;
    Rules::get().UpdateChecksum();

    TestSwapTransaction(true);
    TestSwapTransaction(false);
    TestSwapTransactionWithoutChange(true);
    
    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}