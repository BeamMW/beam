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
#include "wallet/swaps/common.h"
#include "wallet/swaps/swap_transaction.h"
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

namespace
{
    const AmountList kDefaultTestAmounts = { 500, 200, 100, 900 };

    TestBitcoinWallet GetSenderBTCWallet(io::Reactor& reactor, const io::Address& senderAddress, Amount swapAmount)
    {
        TestBitcoinWallet::Options senderOptions;
        senderOptions.m_rawAddress = "2N8N2kr34rcGqHCo3aN6yqniid8a4Mt3FCv";
        senderOptions.m_privateKey = "cSFMca7FAeAgLRgvev5ajC1v1jzprBr1KoefUFFPS8aw3EYwLArM";
        senderOptions.m_refundTx = "0200000001809fc0890cb2724a941dfc3b7213a63b3017b0cddbed4f303be300cb55ddca830100000000ffffffff01e8030000000000001976a9146ed612a79317bc6ade234f299073b945ccb3e76b88ac00000000";
        senderOptions.m_amount = swapAmount;

        return TestBitcoinWallet(reactor, senderAddress, senderOptions);
    }

    TestBitcoinWallet GetReceiverBTCWallet(io::Reactor& reactor, const io::Address& receiverAddress, Amount swapAmount)
    {
        TestBitcoinWallet::Options receiverOptions;
        receiverOptions.m_rawAddress = "2Mvfsv3JiwWXjjwNZD6LQJD4U4zaPAhSyNB";
        receiverOptions.m_privateKey = "cNoRPsNczFw6b7wTuwLx24gSnCPyF3CbvgVmFJYKyfe63nBsGFxr";
        receiverOptions.m_refundTx = "0200000001809fc0890cb2724a941dfc3b7213a63b3017b0cddbed4f303be300cb55ddca830100000000ffffffff01e8030000000000001976a9146ed612a79317bc6ade234f299073b945ccb3e76b88ac00000000";
        receiverOptions.m_amount = swapAmount;

        return TestBitcoinWallet(reactor, receiverAddress, receiverOptions);
    }
}

void TestSwapTransaction(bool isBeamOwnerStart)
{
    cout << "\nTesting atomic swap transaction...\n";

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    int completedCount = 2;
    auto completeAction = [&completedCount, mainReactor](auto)
    {
        --completedCount;
        if (completedCount == 0)
        {
            mainReactor->stop();
            completedCount = 2;
        }
    };

    io::Address senderAddress;
    senderAddress.resolve("127.0.0.1:10400");

    io::Address receiverAddress;
    receiverAddress.resolve("127.0.0.1:10300");

    Amount beamAmount = 300;
    Amount beamFee = 100;
    Amount swapAmount = 2000;
    Amount feeRate = 256;

    BitcoinOptions bobOptions{ "Bob", "123", senderAddress, feeRate };
    BitcoinOptions aliceOptions{ "Alice", "123", receiverAddress, feeRate };
    TestBitcoinWallet senderBtcWallet = GetSenderBTCWallet(*mainReactor, senderAddress, swapAmount);
    TestBitcoinWallet receiverBtcWallet = GetReceiverBTCWallet(*mainReactor, receiverAddress, swapAmount);
    TestWalletRig sender("sender", createSenderWalletDB(false, kDefaultTestAmounts), completeAction);
    TestWalletRig receiver("receiver", createReceiverWalletDB(), completeAction);

    sender.m_Wallet.initBitcoin(*mainReactor, bobOptions);
    receiver.m_Wallet.initBitcoin(*mainReactor, aliceOptions);

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

    TestNode node;
    io::Timer::Ptr timer = io::Timer::create(*mainReactor);
    timer->start(5000, true, [&node]() {node.AddBlock(); });

    mainReactor->run();

    receiverCoins = receiver.GetCoins();
    WALLET_CHECK(receiverCoins.size() == 1);
    WALLET_CHECK(receiverCoins[0].m_ID.m_Value == beamAmount - kMinFeeInGroth);
    WALLET_CHECK(receiverCoins[0].m_status == Coin::Available);
    WALLET_CHECK(receiverCoins[0].m_createTxId == txID);

    auto senderCoins = sender.GetCoins();
    WALLET_CHECK(senderCoins.size() == kDefaultTestAmounts.size() + 1);
    WALLET_CHECK(senderCoins[0].m_ID.m_Value == 500);
    WALLET_CHECK(senderCoins[0].m_status == Coin::Spent);
    WALLET_CHECK(senderCoins[0].m_spentTxId == txID);
    // change
    WALLET_CHECK(senderCoins[4].m_ID.m_Value == 100);
    WALLET_CHECK(senderCoins[4].m_status == Coin::Available);
    WALLET_CHECK(senderCoins[4].m_createTxId == txID);

    // check secret
    NoLeak<uintBig> senderSecretPrivateKey;
    storage::getTxParameter(*sender.m_WalletDB, txID, SubTxIndex::BEAM_REDEEM_TX, TxParameterID::AtomicSwapSecretPrivateKey, senderSecretPrivateKey.V);
    NoLeak<uintBig> receiverSecretPrivateKey;
    storage::getTxParameter(*receiver.m_WalletDB, txID, SubTxIndex::BEAM_REDEEM_TX, TxParameterID::AtomicSwapSecretPrivateKey, receiverSecretPrivateKey.V);
    WALLET_CHECK(senderSecretPrivateKey.V == receiverSecretPrivateKey.V);
}

void TestSwapTransactionWithoutChange(bool isBeamOwnerStart)
{
    cout << "\nTesting atomic swap transaction...\n";

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    int completedCount = 2;
    auto completeAction = [&completedCount, mainReactor](auto)
    {
        --completedCount;
        if (completedCount == 0)
        {
            mainReactor->stop();
            completedCount = 2;
        }
    };

    io::Address senderAddress;
    senderAddress.resolve("127.0.0.1:10400");

    io::Address receiverAddress;
    receiverAddress.resolve("127.0.0.1:10300");

    Amount beamAmount = 400;
    Amount beamFee = 100;
    Amount swapAmount = 2000;
    Amount feeRate = 256;

    BitcoinOptions bobOptions{ "Bob", "123", senderAddress, feeRate };
    BitcoinOptions aliceOptions{ "Alice", "123", receiverAddress, feeRate };
    TestBitcoinWallet senderBtcWallet = GetSenderBTCWallet(*mainReactor, senderAddress, swapAmount);
    TestBitcoinWallet receiverBtcWallet = GetReceiverBTCWallet(*mainReactor, receiverAddress, swapAmount);
    TestWalletRig sender("sender", createSenderWalletDB(false, kDefaultTestAmounts), completeAction);
    TestWalletRig receiver("receiver", createReceiverWalletDB(), completeAction);

    sender.m_Wallet.initBitcoin(*mainReactor, bobOptions);
    receiver.m_Wallet.initBitcoin(*mainReactor, aliceOptions);

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

    TestNode node;
    io::Timer::Ptr timer = io::Timer::create(*mainReactor);
    timer->start(5000, true, [&node]() {node.AddBlock(); });

    mainReactor->run();

    receiverCoins = receiver.GetCoins();
    WALLET_CHECK(receiverCoins.size() == 1);
    WALLET_CHECK(receiverCoins[0].m_ID.m_Value == beamAmount - kMinFeeInGroth);
    WALLET_CHECK(receiverCoins[0].m_status == Coin::Available);
    WALLET_CHECK(receiverCoins[0].m_createTxId == txID);

    auto senderCoins = sender.GetCoins();
    WALLET_CHECK(senderCoins.size() == 4);
    WALLET_CHECK(senderCoins[0].m_ID.m_Value == 500);
    WALLET_CHECK(senderCoins[0].m_status == Coin::Spent);
    WALLET_CHECK(senderCoins[0].m_spentTxId == txID);
}

void TestSwapBTCRefundTransaction()
{
    cout << "\nAtomic swap: testing BTC refund transaction...\n";

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    auto completedAction = [mainReactor](auto)
    {
        mainReactor->stop();
    };

    io::Address senderAddress;
    senderAddress.resolve("127.0.0.1:10400");

    io::Address receiverAddress;
    receiverAddress.resolve("127.0.0.1:10300");

    Amount beamAmount = 300;
    Amount beamFee = 100;
    Amount swapAmount = 2000;
    Amount feeRate = 256;
    uint32_t lockTimeInBlocks = 200;

    BitcoinOptions bobOptions { "Bob", "123", senderAddress, feeRate };
    bobOptions.m_lockTimeInBlocks = lockTimeInBlocks;
    BitcoinOptions aliceOptions { "Alice", "123", receiverAddress, feeRate };
    aliceOptions.m_lockTimeInBlocks = lockTimeInBlocks;

    TestBitcoinWallet senderBtcWallet = GetSenderBTCWallet(*mainReactor, senderAddress, swapAmount);
    TestBitcoinWallet receiverBtcWallet = GetReceiverBTCWallet(*mainReactor, receiverAddress, swapAmount);
    auto sender = std::make_unique<TestWalletRig>("sender", createSenderWalletDB(false, kDefaultTestAmounts), completedAction);
    auto receiver = std::make_shared<TestWalletRig>("receiver", createReceiverWalletDB(), completedAction);

    receiverBtcWallet.addPeer(senderAddress);
    sender->m_Wallet.initBitcoin(*mainReactor, bobOptions);
    receiver->m_Wallet.initBitcoin(*mainReactor, aliceOptions);
    receiver->m_Wallet.initSwapConditions(beamAmount, swapAmount, wallet::AtomicSwapCoin::Bitcoin, false);

    TxID txID = sender->m_Wallet.swap_coins(sender->m_WalletID, receiver->m_WalletID, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, true);

    auto receiverCoins = receiver->GetCoins();
    WALLET_CHECK(receiverCoins.empty());

    TestNode node;
    io::Timer::Ptr timer = io::Timer::create(*mainReactor);
    timer->start(5000, true, [&node]() {node.AddBlock(); });

    io::AsyncEvent::Ptr eventToUpdate;

    eventToUpdate = io::AsyncEvent::create(*mainReactor, [&sender, receiver, txID, &eventToUpdate, &timer]()
    {
        if (sender)
        {
            wallet::AtomicSwapTransaction::State txState = wallet::AtomicSwapTransaction::State::Initial;
            storage::getTxParameter(*sender->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
            if (txState == wallet::AtomicSwapTransaction::State::HandlingContractTX)
            {
                // delete sender to simulate refund on BTC side
                sender.reset();
            }
            eventToUpdate->post();
        }
        else
        {
            wallet::AtomicSwapTransaction::State txState = wallet::AtomicSwapTransaction::State::Initial;
            storage::getTxParameter(*receiver->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
            if (txState != wallet::AtomicSwapTransaction::State::SendingRefundTX)
            {
                // speed-up test
                timer->restart(50, true);
            }
        }
    });

    eventToUpdate->post();
    mainReactor->run();

    // validate receiver TX state
    wallet::AtomicSwapTransaction::State txState = wallet::AtomicSwapTransaction::State::Initial;
    storage::getTxParameter(*receiver->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
    WALLET_CHECK(txState == wallet::AtomicSwapTransaction::State::Refunded);
    receiverCoins = receiver->GetCoins();
    WALLET_CHECK(receiverCoins.size() == 0);

    // TODO: add check BTC balance
}

void TestSwapBeamRefundTransaction()
{
    cout << "\nAtomic swap: testing Beam refund transaction...\n";

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    auto completedAction = [mainReactor](auto)
    {
        mainReactor->stop();
    };

    io::Address senderAddress;
    senderAddress.resolve("127.0.0.1:10400");

    io::Address receiverAddress;
    receiverAddress.resolve("127.0.0.1:10300");

    Amount beamAmount = 300;
    Amount beamFee = 100;
    Amount swapAmount = 2000;
    Amount feeRate = 256;

    BitcoinOptions bobOptions{ "Bob", "123", senderAddress, feeRate };
    BitcoinOptions aliceOptions{ "Alice", "123", receiverAddress, feeRate };
    TestBitcoinWallet senderBtcWallet = GetSenderBTCWallet(*mainReactor, senderAddress, swapAmount);
    TestBitcoinWallet receiverBtcWallet = GetReceiverBTCWallet(*mainReactor, receiverAddress, swapAmount);
    auto sender = std::make_unique<TestWalletRig>("sender", createSenderWalletDB(false, kDefaultTestAmounts), completedAction);
    auto receiver = std::make_unique<TestWalletRig>("receiver", createReceiverWalletDB(), completedAction);

    receiverBtcWallet.addPeer(senderAddress);
    sender->m_Wallet.initBitcoin(*mainReactor, bobOptions);
    receiver->m_Wallet.initBitcoin(*mainReactor, aliceOptions);
    receiver->m_Wallet.initSwapConditions(beamAmount, swapAmount, wallet::AtomicSwapCoin::Bitcoin, false);

    TxID txID = sender->m_Wallet.swap_coins(sender->m_WalletID, receiver->m_WalletID, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, true);

    auto receiverCoins = receiver->GetCoins();
    WALLET_CHECK(receiverCoins.empty());

    TestNode node;
    io::AsyncEvent::Ptr eventToUpdate;

    eventToUpdate = io::AsyncEvent::create(*mainReactor, [&sender, &receiver, txID, &eventToUpdate, &node]()
    {
        if (receiver)
        {
            wallet::AtomicSwapTransaction::State txState = wallet::AtomicSwapTransaction::State::Initial;
            storage::getTxParameter(*receiver->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
            if (txState == wallet::AtomicSwapTransaction::State::SendingBeamRedeemTX)
            {
                // delete receiver to simulate refund on Beam side
                receiver.reset();
            }
            eventToUpdate->post();
        }
        else
        {
            wallet::AtomicSwapTransaction::State txState = wallet::AtomicSwapTransaction::State::Initial;
            storage::getTxParameter(*sender->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
            if (txState != wallet::AtomicSwapTransaction::State::SendingBeamRefundTX)
            {
                // speed-up test
                node.AddBlock();
                eventToUpdate->post();
            }
        }
    });

    io::Timer::Ptr timer = io::Timer::create(*mainReactor);
    timer->start(5000, true, [&node]() {node.AddBlock(); });

    eventToUpdate->post();
    mainReactor->run();

    // validate sender TX state
    wallet::AtomicSwapTransaction::State txState = wallet::AtomicSwapTransaction::State::Initial;
    storage::getTxParameter(*sender->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
    WALLET_CHECK(txState == wallet::AtomicSwapTransaction::State::Refunded);

    auto senderCoins = sender->GetCoins();
    WALLET_CHECK(senderCoins.size() == 6);
    WALLET_CHECK(senderCoins[0].m_ID.m_Value == 500);
    WALLET_CHECK(senderCoins[0].m_status == Coin::Spent);
    WALLET_CHECK(senderCoins[0].m_spentTxId == txID);

    // change of Beam LockTx
    WALLET_CHECK(senderCoins[4].m_ID.m_Value == 100);
    WALLET_CHECK(senderCoins[4].m_status == Coin::Available);
    WALLET_CHECK(senderCoins[4].m_createTxId == txID);

    // Refund
    WALLET_CHECK(senderCoins[5].m_ID.m_Value == beamAmount - kMinFeeInGroth);
    WALLET_CHECK(senderCoins[5].m_status == Coin::Available);
    WALLET_CHECK(senderCoins[5].m_createTxId == txID);
}

void ExpireByResponseTime(bool isBeamSide)
{
    // Simulate swap transaction without response from second side

    cout << "\nAtomic swap: testing expired transaction on " << (isBeamSide ? "Beam" : "BTC") << " side...\n";

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    auto completedAction = [mainReactor](auto)
    {
        mainReactor->stop();
    };

    io::Address senderAddress;
    senderAddress.resolve("127.0.0.1:10400");

    Amount beamAmount = 300;
    Amount beamFee = 100;
    Amount swapAmount = 2000;
    Amount feeRate = 256;
    Height lifetime = 100;
    Height responseTime = 100;

    BitcoinOptions aliceOptions{ "Alice", "123", senderAddress, feeRate };
    TestBitcoinWallet senderBtcWallet = GetSenderBTCWallet(*mainReactor, senderAddress, swapAmount);
    auto sender = std::make_unique<TestWalletRig>("sender", createSenderWalletDB(false, kDefaultTestAmounts), completedAction);

    sender->m_Wallet.initBitcoin(*mainReactor, aliceOptions);

    WalletAddress receiverWalletAddress = storage::createAddress(*createReceiverWalletDB());
    WalletID receiverWalletID = receiverWalletAddress.m_walletID;

    TxID txID = sender->m_Wallet.swap_coins(sender->m_WalletID, receiverWalletID, beamAmount, beamFee,
        wallet::AtomicSwapCoin::Bitcoin, swapAmount, isBeamSide, lifetime, responseTime);

    TestNode node;
    io::Timer::Ptr timer = io::Timer::create(*mainReactor);
    timer->start(50, true, [&node]() {node.AddBlock(); });

    mainReactor->run();

    // validate sender TX state
    wallet::AtomicSwapTransaction::State txState = wallet::AtomicSwapTransaction::State::Initial;
    storage::getTxParameter(*sender->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
    WALLET_CHECK(txState == wallet::AtomicSwapTransaction::State::Failed);

    TxFailureReason reason = TxFailureReason::Unknown;
    storage::getTxParameter(*sender->m_WalletDB, txID, TxParameterID::InternalFailureReason, reason);
    WALLET_CHECK(reason == TxFailureReason::TransactionExpired);

    if (isBeamSide)
    {
        auto senderCoins = sender->GetCoins();
        WALLET_CHECK(senderCoins.size() == 4);
        WALLET_CHECK(senderCoins[0].m_ID.m_Value == 500);
        WALLET_CHECK(senderCoins[0].m_status == Coin::Available);
    }
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

    TestSwapBTCRefundTransaction();
    TestSwapBeamRefundTransaction();

    ExpireByResponseTime(true);
    ExpireByResponseTime(false);

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}
