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
#include "wallet/wallet_transaction.h"
#include "keykeeper/local_private_key_keeper.h"
#include "wallet/secstring.h"
#include "wallet/swaps/common.h"
#include "wallet/swaps/swap_transaction.h"
#include "wallet/swaps/utils.h"
#include "wallet/swaps/second_side.h"
#include "wallet/bitcoin/bitcoin.h"

#include "http/http_client.h"
#include "utility/test_helpers.h"
#include "core/radixtree.h"
#include "core/unittest/mini_blockchain.h"
#include "core/negotiator.h"
#include "node/node.h"
#include "utility/io/sslserver.h"

#include "test_helpers.h"

#include <boost/filesystem.hpp>
#include <boost/intrusive/list.hpp>
#include <string_view>

using namespace beam;
using namespace std;
using namespace ECC;

WALLET_TEST_INIT

#include "wallet_test_environment.cpp"
#include "swap_test_environment.cpp"

namespace
{
    const AmountList kDefaultTestAmounts = { 500, 200, 100, 900 };
    const Height kNodeStartHeight = 145;

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

bitcoin::ISettingsProvider::Ptr InitSettingsProvider(IWalletDB::Ptr walletDB, std::shared_ptr<bitcoin::Settings> settings)
{
    auto settingsProvider = std::make_shared<bitcoin::SettingsProvider>(walletDB);
    settingsProvider->SetSettings(*settings);
    return settingsProvider;
}

void InitBitcoin(Wallet& wallet, IWalletDB::Ptr walletDB, io::Reactor& reactor, bitcoin::ISettingsProvider& settingsProvider)
{
    auto creator = std::make_shared<AtomicSwapTransaction::Creator>(walletDB);
    auto bridge = std::make_shared<bitcoin::BitcoinCore017>(reactor, settingsProvider);
    // TODO should refactored this code
    auto bitcoinBridgeCreator = [bridge]() -> bitcoin::IBridge::Ptr
    {
        return bridge;
    };
    auto factory = wallet::MakeSecondSideFactory<BitcoinSide, bitcoin::BitcoinCore017, bitcoin::ISettingsProvider>(bitcoinBridgeCreator, settingsProvider);
    creator->RegisterFactory(AtomicSwapCoin::Bitcoin, factory);
    wallet.RegisterTransactionType(TxType::AtomicSwap, std::static_pointer_cast<BaseTransaction::Creator>(creator));
}

void InitElectrum(Wallet& wallet, IWalletDB::Ptr walletDB, io::Reactor& reactor, bitcoin::ISettingsProvider& settingsProvider)
{
    auto creator = std::make_shared<AtomicSwapTransaction::Creator>(walletDB);
    auto bridge = std::make_shared<bitcoin::Electrum>(reactor, settingsProvider);
    // TODO should refactored this code
    auto bitcoinBridgeCreator = [bridge]() -> bitcoin::IBridge::Ptr
    {
        return bridge;
    };
    auto factory = wallet::MakeSecondSideFactory<BitcoinSide, bitcoin::Electrum, bitcoin::ISettingsProvider>(bitcoinBridgeCreator, settingsProvider);
    creator->RegisterFactory(AtomicSwapCoin::Bitcoin, factory);
    wallet.RegisterTransactionType(TxType::AtomicSwap, std::static_pointer_cast<BaseTransaction::Creator>(creator));
}

void TestSwapTransaction(bool isBeamOwnerStart, beam::Height fork1Height)
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
    uint16_t btcTxMinConfirmations = 2;

    auto senderWalletDB = createSenderWalletDB(0, 0);
    auto binaryTreasury = createTreasury(senderWalletDB, kDefaultTestAmounts);

    auto bobSettings = std::make_shared<bitcoin::Settings>();
    bobSettings->SetConnectionOptions({ "Bob", "123", senderAddress });
    bobSettings->SetFeeRate(feeRate);
    bobSettings->SetTxMinConfirmations(btcTxMinConfirmations);

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetConnectionOptions({ "Alice", "123", receiverAddress });
    aliceSettings->SetFeeRate(feeRate);
    aliceSettings->SetTxMinConfirmations(btcTxMinConfirmations);

    TestBitcoinWallet senderBtcWallet = GetSenderBTCWallet(*mainReactor, senderAddress, swapAmount);
    TestBitcoinWallet receiverBtcWallet = GetReceiverBTCWallet(*mainReactor, receiverAddress, swapAmount);

    auto senderSP = InitSettingsProvider(senderWalletDB, bobSettings);
    auto receiverWalletDB = createReceiverWalletDB();
    auto receiverSP = InitSettingsProvider(receiverWalletDB, aliceSettings);

    TestWalletRig sender("sender", senderWalletDB, completeAction);
    TestWalletRig receiver("receiver", receiverWalletDB, completeAction);

    InitBitcoin(sender.m_Wallet, sender.m_WalletDB, *mainReactor, *senderSP);
    InitBitcoin(receiver.m_Wallet, receiver.m_WalletDB, *mainReactor, *receiverSP);

    WALLET_CHECK(senderSP->CanModify() == true);
    WALLET_CHECK(receiverSP->CanModify() == true);

    receiverBtcWallet.addPeer(senderAddress);

    TxID txID = { {0} };

    auto receiverCoins = receiver.GetCoins();
    WALLET_CHECK(receiverCoins.empty());

    Node node;

    NodeObserver observer([&]()
    {
        auto cursor = node.get_Processor().m_Cursor;
        if (cursor.m_Sid.m_Height == fork1Height + 5)
        {
            auto currentHeight = cursor.m_Sid.m_Height;

            auto parameters = CreateSwapParameters()
                .SetParameter(TxParameterID::Amount, beamAmount)
                .SetParameter(TxParameterID::Fee, beamFee)
                .SetParameter(TxParameterID::AtomicSwapCoin, wallet::AtomicSwapCoin::Bitcoin)
                .SetParameter(TxParameterID::AtomicSwapAmount, swapAmount)
                .SetParameter(TxParameterID::MinHeight, currentHeight)
                .SetParameter(TxParameterID::PeerResponseTime, kDefaultTxResponseTime)
                .SetParameter(TxParameterID::IsInitiator, false);

            if (isBeamOwnerStart)
            {
                parameters.SetParameter(TxParameterID::MyID, receiver.m_WalletID)
                    .SetParameter(TxParameterID::AtomicSwapIsBeamSide, false);
                
                receiver.m_Wallet.StartTransaction(parameters);
                txID = sender.m_Wallet.StartTransaction(AcceptSwapParameters(parameters, sender.m_WalletID));
            }
            else
            {
                parameters.SetParameter(TxParameterID::MyID, sender.m_WalletID)
                    .SetParameter(TxParameterID::AtomicSwapIsBeamSide, true);

                sender.m_Wallet.StartTransaction(parameters);
                txID = receiver.m_Wallet.StartTransaction(AcceptSwapParameters(parameters, receiver.m_WalletID));
            }
        }
    });

    InitNodeToTest(node, binaryTreasury, &observer, 32125, 2000);

    mainReactor->run();

    WALLET_CHECK(senderSP->CanModify() == true);
    WALLET_CHECK(receiverSP->CanModify() == true);

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
    WALLET_CHECK(senderSecretPrivateKey.V != Zero && senderSecretPrivateKey.V == receiverSecretPrivateKey.V);
}

void TestElectrumSwapTransaction(bool isBeamOwnerStart, beam::Height fork1Height)
{
    cout << "\nTesting atomic swap transaction on electrum...\n";

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

    std::string address("127.0.0.1:10400");

    Amount beamAmount = 300;
    Amount beamFee = 100;
    Amount swapAmount = 200000;
    Amount feeRate = 80000;
    uint16_t btcTxMinConfirmations = 2;

    auto senderWalletDB = createSenderWalletDB(0, 0);
    auto binaryTreasury = createTreasury(senderWalletDB, kDefaultTestAmounts);

    auto bobSettings = std::make_shared<bitcoin::Settings>();
    bobSettings->SetElectrumConnectionOptions({ address, {"unveil", "shadow", "gold", "piece", "salad", "parent", "leisure", "obtain", "wave", "eternal", "suggest", "artwork"}, bitcoin::getAddressVersion() });
    bobSettings->SetFeeRate(feeRate);
    bobSettings->SetTxMinConfirmations(btcTxMinConfirmations);

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetElectrumConnectionOptions({ address, {"rib", "genuine", "fury", "advance", "train", "capable", "rough", "silk", "march", "vague", "notice", "sphere"}, bitcoin::getAddressVersion() });
    aliceSettings->SetFeeRate(feeRate);
    aliceSettings->SetTxMinConfirmations(btcTxMinConfirmations);

    TestElectrumWallet btcWallet(*mainReactor, address);
    auto senderSP = InitSettingsProvider(senderWalletDB, bobSettings);
    auto receiverWalletDB = createReceiverWalletDB();
    auto receiverSP = InitSettingsProvider(receiverWalletDB, aliceSettings);
    TestWalletRig sender("sender", senderWalletDB, completeAction);
    TestWalletRig receiver("receiver", receiverWalletDB, completeAction);

    InitElectrum(sender.m_Wallet, sender.m_WalletDB, *mainReactor, *senderSP);
    InitElectrum(receiver.m_Wallet, receiver.m_WalletDB, *mainReactor, *receiverSP);

    TxID txID = { {0} };

    auto receiverCoins = receiver.GetCoins();
    WALLET_CHECK(receiverCoins.empty());

    Node node;

    NodeObserver observer([&]()
        {
            auto cursor = node.get_Processor().m_Cursor;
            if (cursor.m_Sid.m_Height == fork1Height + 5)
            {
                auto currentHeight = cursor.m_Sid.m_Height;

                auto parameters = CreateSwapParameters()
                    .SetParameter(TxParameterID::Amount, beamAmount)
                    .SetParameter(TxParameterID::Fee, beamFee)
                    .SetParameter(TxParameterID::AtomicSwapCoin, wallet::AtomicSwapCoin::Bitcoin)
                    .SetParameter(TxParameterID::AtomicSwapAmount, swapAmount)
                    .SetParameter(TxParameterID::MinHeight, currentHeight)
                    .SetParameter(TxParameterID::PeerResponseTime, kDefaultTxResponseTime)
                    .SetParameter(TxParameterID::IsInitiator, false);

                if (isBeamOwnerStart)
                {
                    parameters.SetParameter(TxParameterID::MyID, receiver.m_WalletID)
                        .SetParameter(TxParameterID::AtomicSwapIsBeamSide, false);

                    receiver.m_Wallet.StartTransaction(parameters);
                    txID = sender.m_Wallet.StartTransaction(AcceptSwapParameters(parameters, sender.m_WalletID));
                }
                else
                {
                    parameters.SetParameter(TxParameterID::MyID, sender.m_WalletID)
                        .SetParameter(TxParameterID::AtomicSwapIsBeamSide, true);

                    sender.m_Wallet.StartTransaction(parameters);
                    txID = receiver.m_Wallet.StartTransaction(AcceptSwapParameters(parameters, receiver.m_WalletID));
                }
            }
        });

    InitNodeToTest(node, binaryTreasury, &observer, 32125, 2000);

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
    WALLET_CHECK(senderSecretPrivateKey.V != Zero && senderSecretPrivateKey.V == receiverSecretPrivateKey.V);
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
    uint16_t btcTxMinConfirmations = 2;

    auto bobSettings = std::make_shared<bitcoin::Settings>();
    bobSettings->SetConnectionOptions({ "Bob", "123", senderAddress });
    bobSettings->SetFeeRate(feeRate);
    bobSettings->SetTxMinConfirmations(btcTxMinConfirmations);

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetConnectionOptions({ "Alice", "123", receiverAddress });
    aliceSettings->SetFeeRate(feeRate);
    aliceSettings->SetTxMinConfirmations(btcTxMinConfirmations);

    TestBitcoinWallet senderBtcWallet = GetSenderBTCWallet(*mainReactor, senderAddress, swapAmount);
    TestBitcoinWallet receiverBtcWallet = GetReceiverBTCWallet(*mainReactor, receiverAddress, swapAmount);
    auto senderWalletDB = createSenderWalletDB(false, kDefaultTestAmounts);
    auto senderSP = InitSettingsProvider(senderWalletDB, bobSettings);
    auto receiverWalletDB = createReceiverWalletDB();
    auto receiverSP = InitSettingsProvider(receiverWalletDB, aliceSettings);
    TestWalletRig sender("sender", senderWalletDB, completeAction);
    TestWalletRig receiver("receiver", receiverWalletDB, completeAction);

    InitBitcoin(sender.m_Wallet, sender.m_WalletDB, *mainReactor, *senderSP);
    InitBitcoin(receiver.m_Wallet, receiver.m_WalletDB, *mainReactor, *receiverSP);

    receiverBtcWallet.addPeer(senderAddress);

    TestNode node{ TestNode::NewBlockFunc(), kNodeStartHeight };
    Height currentHeight = node.m_Blockchain.m_mcm.m_vStates.size();
    TxID txID = { {0} };

    if (isBeamOwnerStart)
    {
        auto parameters = InitNewSwap(receiver.m_WalletID, currentHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, false);
        receiver.m_Wallet.StartTransaction(parameters);
        txID = sender.m_Wallet.StartTransaction(AcceptSwapParameters(parameters, sender.m_WalletID));
    }
    else
    {
        auto parameters = InitNewSwap(sender.m_WalletID, currentHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, true);
        sender.m_Wallet.StartTransaction(parameters);
        txID = receiver.m_Wallet.StartTransaction(AcceptSwapParameters(parameters, receiver.m_WalletID));
    }

    auto receiverCoins = receiver.GetCoins();
    WALLET_CHECK(receiverCoins.empty());

    io::Timer::Ptr timer = io::Timer::create(*mainReactor);
    timer->start(1000, true, [&node]() {node.AddBlock(); });

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
    uint32_t lockTimeInBlocks = 100;

    auto bobSettings = std::make_shared<bitcoin::Settings>();
    bobSettings->SetConnectionOptions({ "Bob", "123", senderAddress });
    bobSettings->SetFeeRate(feeRate);
    bobSettings->SetLockTimeInBlocks(lockTimeInBlocks);

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetConnectionOptions({ "Alice", "123", receiverAddress });
    aliceSettings->SetFeeRate(feeRate);
    aliceSettings->SetLockTimeInBlocks(lockTimeInBlocks);

    TestBitcoinWallet senderBtcWallet = GetSenderBTCWallet(*mainReactor, senderAddress, swapAmount);
    TestBitcoinWallet receiverBtcWallet = GetReceiverBTCWallet(*mainReactor, receiverAddress, swapAmount);
    receiverBtcWallet.addPeer(senderAddress);

    auto senderWalletDB = createSenderWalletDB(false, kDefaultTestAmounts);
    auto senderSP = InitSettingsProvider(senderWalletDB, bobSettings);
    auto receiverWalletDB = createReceiverWalletDB();
    auto receiverSP = InitSettingsProvider(receiverWalletDB, aliceSettings);
    auto sender = std::make_unique<TestWalletRig>("sender", senderWalletDB, completedAction);
    auto receiver = std::make_shared<TestWalletRig>("receiver", receiverWalletDB, completedAction);

    InitBitcoin(sender->m_Wallet, sender->m_WalletDB, *mainReactor, *senderSP);
    InitBitcoin(receiver->m_Wallet, receiver->m_WalletDB, *mainReactor, *receiverSP);

    TestNode node{ TestNode::NewBlockFunc(), kNodeStartHeight };
    Height currentHeight = node.m_Blockchain.m_mcm.m_vStates.size();

    auto parameters = InitNewSwap(receiver->m_WalletID, currentHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, false);
    receiver->m_Wallet.StartTransaction(parameters);
    TxID txID = sender->m_Wallet.StartTransaction(AcceptSwapParameters(parameters, sender->m_WalletID));

    auto receiverCoins = receiver->GetCoins();
    WALLET_CHECK(receiverCoins.empty());

    io::Timer::Ptr timer = io::Timer::create(*mainReactor);
    timer->start(3000, true, [&node]() {node.AddBlock(); });

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

void TestElectrumSwapBTCRefundTransaction()
{
    cout << "\nAtomic swap: testing BTC refund transaction on electrum...\n";

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    auto completedAction = [mainReactor](auto)
    {
        mainReactor->stop();
    };

    std::string address("127.0.0.1:10400");

    Amount beamAmount = 300;
    Amount beamFee = 100;
    Amount swapAmount = 200000;
    Amount feeRate = 80000;
    uint32_t lockTimeInBlocks = 100;

    auto bobSettings = std::make_shared<bitcoin::Settings>();
    bobSettings->SetElectrumConnectionOptions({ address, {"unveil", "shadow", "gold", "piece", "salad", "parent", "leisure", "obtain", "wave", "eternal", "suggest", "artwork"}, bitcoin::getAddressVersion() });
    bobSettings->SetFeeRate(feeRate);
    bobSettings->SetLockTimeInBlocks(lockTimeInBlocks);

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetElectrumConnectionOptions({ address, {"rib", "genuine", "fury", "advance", "train", "capable", "rough", "silk", "march", "vague", "notice", "sphere"}, bitcoin::getAddressVersion() });
    aliceSettings->SetFeeRate(feeRate);
    aliceSettings->SetLockTimeInBlocks(lockTimeInBlocks);

    auto senderWalletDB = createSenderWalletDB(false, kDefaultTestAmounts);
    auto senderSP = InitSettingsProvider(senderWalletDB, bobSettings);
    auto receiverWalletDB = createReceiverWalletDB();
    auto receiverSP = InitSettingsProvider(receiverWalletDB, aliceSettings);
    TestElectrumWallet btcWallet(*mainReactor, address);
    auto sender = std::make_unique<TestWalletRig>("sender", senderWalletDB, completedAction);
    auto receiver = std::make_shared<TestWalletRig>("receiver", receiverWalletDB, completedAction);

    InitElectrum(sender->m_Wallet, sender->m_WalletDB, *mainReactor, *senderSP);
    InitElectrum(receiver->m_Wallet, receiver->m_WalletDB, *mainReactor, *receiverSP);

    TestNode node{ TestNode::NewBlockFunc(), kNodeStartHeight };
    Height currentHeight = node.m_Blockchain.m_mcm.m_vStates.size();
    auto parameters = InitNewSwap(receiver->m_WalletID, currentHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, false);

    receiver->m_Wallet.StartTransaction(parameters);
    TxID txID = sender->m_Wallet.StartTransaction(AcceptSwapParameters(parameters, sender->m_WalletID));

    auto receiverCoins = receiver->GetCoins();
    WALLET_CHECK(receiverCoins.empty());

    io::Timer::Ptr timer = io::Timer::create(*mainReactor);
    timer->start(3000, true, [&node]() {node.AddBlock(); });

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

    auto bobSettings = std::make_shared<bitcoin::Settings>();
    bobSettings->SetConnectionOptions({ "Bob", "123", senderAddress });
    bobSettings->SetFeeRate(feeRate);

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetConnectionOptions({ "Alice", "123", receiverAddress });
    aliceSettings->SetFeeRate(feeRate);
    TestBitcoinWallet senderBtcWallet = GetSenderBTCWallet(*mainReactor, senderAddress, swapAmount);
    TestBitcoinWallet receiverBtcWallet = GetReceiverBTCWallet(*mainReactor, receiverAddress, swapAmount);
    receiverBtcWallet.addPeer(senderAddress);

    auto senderWalletDB = createSenderWalletDB(false, kDefaultTestAmounts);
    auto senderSP = InitSettingsProvider(senderWalletDB, bobSettings);
    auto receiverWalletDB = createReceiverWalletDB();
    auto receiverSP = InitSettingsProvider(receiverWalletDB, aliceSettings);
    auto sender = std::make_unique<TestWalletRig>("sender", senderWalletDB, completedAction);
    auto receiver = std::make_unique<TestWalletRig>("receiver", receiverWalletDB, completedAction);

    InitBitcoin(sender->m_Wallet, sender->m_WalletDB, *mainReactor, *senderSP);
    InitBitcoin(receiver->m_Wallet, receiver->m_WalletDB, *mainReactor, *receiverSP);

    TestNode node{ TestNode::NewBlockFunc(), kNodeStartHeight };
    Height currentHeight = node.m_Blockchain.m_mcm.m_vStates.size();
    auto parameters = InitNewSwap(receiver->m_WalletID, currentHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, false);
    receiver->m_Wallet.StartTransaction(parameters);
    TxID txID = sender->m_Wallet.StartTransaction(AcceptSwapParameters(parameters, sender->m_WalletID));

    auto receiverCoins = receiver->GetCoins();
    WALLET_CHECK(receiverCoins.empty());

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
    timer->start(3000, true, [&node]() {node.AddBlock(); });

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

void TestElectrumSwapBeamRefundTransaction()
{
    cout << "\nAtomic swap: testing Beam refund transaction on electrum...\n";

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    auto completedAction = [mainReactor](auto)
    {
        mainReactor->stop();
    };

    std::string address("127.0.0.1:10400");

    Amount beamAmount = 300;
    Amount beamFee = 100;
    Amount swapAmount = 200000;
    Amount feeRate = 80000;

    auto bobSettings = std::make_shared<bitcoin::Settings>();
    bobSettings->SetElectrumConnectionOptions({ address, {"unveil", "shadow", "gold", "piece", "salad", "parent", "leisure", "obtain", "wave", "eternal", "suggest", "artwork"}, bitcoin::getAddressVersion() });
    bobSettings->SetFeeRate(feeRate);

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetElectrumConnectionOptions({ address, {"rib", "genuine", "fury", "advance", "train", "capable", "rough", "silk", "march", "vague", "notice", "sphere"}, bitcoin::getAddressVersion() });
    aliceSettings->SetFeeRate(feeRate);
    TestElectrumWallet btcWallet(*mainReactor, address);
    auto senderWalletDB = createSenderWalletDB(false, kDefaultTestAmounts);
    auto senderSP = InitSettingsProvider(senderWalletDB, bobSettings);
    auto receiverWalletDB = createReceiverWalletDB();
    auto receiverSP = InitSettingsProvider(receiverWalletDB, aliceSettings);
    auto sender = std::make_unique<TestWalletRig>("sender", senderWalletDB, completedAction);
    auto receiver = std::make_unique<TestWalletRig>("receiver", receiverWalletDB, completedAction);

    InitElectrum(sender->m_Wallet, sender->m_WalletDB, *mainReactor, *senderSP);
    InitElectrum(receiver->m_Wallet, receiver->m_WalletDB, *mainReactor, *receiverSP);

    TestNode node{ TestNode::NewBlockFunc(), kNodeStartHeight };
    Height currentHeight = node.m_Blockchain.m_mcm.m_vStates.size();
    auto parameters = InitNewSwap(receiver->m_WalletID, currentHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, false);
    receiver->m_Wallet.StartTransaction(parameters);
    TxID txID = sender->m_Wallet.StartTransaction(AcceptSwapParameters(parameters, sender->m_WalletID));

    auto receiverCoins = receiver->GetCoins();
    WALLET_CHECK(receiverCoins.empty());

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
    timer->start(3000, true, [&node]() {node.AddBlock(); });

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

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetConnectionOptions({ "Alice", "123", senderAddress });
    aliceSettings->SetFeeRate(feeRate);
    TestBitcoinWallet senderBtcWallet = GetSenderBTCWallet(*mainReactor, senderAddress, swapAmount);
    auto senderWalletDB = createSenderWalletDB(false, kDefaultTestAmounts);
    auto senderSP = InitSettingsProvider(senderWalletDB, aliceSettings);
    auto sender = std::make_unique<TestWalletRig>("sender", senderWalletDB, completedAction);

    InitBitcoin(sender->m_Wallet, sender->m_WalletDB, *mainReactor, *senderSP);

    auto db = createReceiverWalletDB();
    auto keyKeeper = std::make_shared<LocalPrivateKeyKeeper>(db, db->get_MasterKdf());
    WalletAddress receiverWalletAddress = storage::createAddress(*db, keyKeeper);
    WalletID receiverWalletID = receiverWalletAddress.m_walletID;

    TestNode node{ TestNode::NewBlockFunc(), kNodeStartHeight };
    Height currentHeight = node.m_Blockchain.m_mcm.m_vStates.size();
    auto swapParameters = InitNewSwap(receiverWalletID, currentHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, !isBeamSide, lifetime, responseTime);
    TxID txID = sender->m_Wallet.StartTransaction(AcceptSwapParameters(swapParameters, sender->m_WalletID));
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

void TestSwapCancelTransaction(bool isSender, wallet::AtomicSwapTransaction::State testingState)
{
    cout << "\nAtomic swap: testing cancel transaction (" << (isSender ? "sender" : "receiver") << ", " << wallet::getSwapTxStatus(testingState) << ")...\n";

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

    auto bobSettings = std::make_shared<bitcoin::Settings>();
    bobSettings->SetConnectionOptions({ "Bob", "123", senderAddress });
    bobSettings->SetFeeRate(feeRate);

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetConnectionOptions({ "Alice", "123", receiverAddress });
    aliceSettings->SetFeeRate(feeRate);
    TestBitcoinWallet senderBtcWallet = GetSenderBTCWallet(*mainReactor, senderAddress, swapAmount);
    TestBitcoinWallet receiverBtcWallet = GetReceiverBTCWallet(*mainReactor, receiverAddress, swapAmount);
    auto senderWalletDB = createSenderWalletDB(false, kDefaultTestAmounts);
    auto senderSP = InitSettingsProvider(senderWalletDB, bobSettings);
    auto receiverWalletDB = createReceiverWalletDB();
    auto receiverSP = InitSettingsProvider(receiverWalletDB, aliceSettings);
    auto sender = std::make_unique<TestWalletRig>("sender", senderWalletDB, isSender ? Wallet::TxCompletedAction() : completedAction);
    auto receiver = std::make_unique<TestWalletRig>("receiver", receiverWalletDB, isSender ? completedAction : Wallet::TxCompletedAction());

    receiverBtcWallet.addPeer(senderAddress);
    InitBitcoin(sender->m_Wallet, sender->m_WalletDB, *mainReactor, *senderSP);
    InitBitcoin(receiver->m_Wallet, receiver->m_WalletDB, *mainReactor, *receiverSP);
    TestNode node{ TestNode::NewBlockFunc(), kNodeStartHeight };
    Height currentHeight = node.m_Blockchain.m_mcm.m_vStates.size();
    auto parameters = InitNewSwap(receiver->m_WalletID, currentHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, false);
    receiver->m_Wallet.StartTransaction(parameters);
    TxID txID = sender->m_Wallet.StartTransaction(AcceptSwapParameters(parameters, sender->m_WalletID));

    auto receiverCoins = receiver->GetCoins();
    WALLET_CHECK(receiverCoins.empty());

    io::AsyncEvent::Ptr eventToUpdate;

    eventToUpdate = io::AsyncEvent::create(*mainReactor, [&walletRig = isSender ? sender: receiver, testingState, txID, &eventToUpdate]()
    {
        wallet::AtomicSwapTransaction::State txState = wallet::AtomicSwapTransaction::State::Initial;
        storage::getTxParameter(*walletRig->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
        if (txState == testingState)
        {
            walletRig->m_Wallet.CancelTransaction(txID);
        }
        else
        {
            eventToUpdate->post();
        }
    });

    io::Timer::Ptr timer = io::Timer::create(*mainReactor);
    timer->start(1000, true, [&node]() {node.AddBlock(); });

    eventToUpdate->post();
    mainReactor->run();

    // validate sender TX state
    wallet::AtomicSwapTransaction::State txState = wallet::AtomicSwapTransaction::State::Initial;
    storage::getTxParameter(*sender->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
    WALLET_CHECK(txState == (isSender ? wallet::AtomicSwapTransaction::State::Canceled : wallet::AtomicSwapTransaction::State::Failed));

    storage::getTxParameter(*receiver->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
    WALLET_CHECK(txState == (isSender ? wallet::AtomicSwapTransaction::State::Failed : wallet::AtomicSwapTransaction::State::Canceled));

    auto senderCoins = sender->GetCoins();
    WALLET_CHECK(senderCoins.size() == 4);

    for (const auto& coin : senderCoins)
    {
        WALLET_CHECK(coin.m_status == Coin::Available);
    }    
}

void TestSwap120Blocks()
{
    cout << "\nAtomic swap: testing 120 blocks ...\n";

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

    auto bobSettings = std::make_shared<bitcoin::Settings>();
    bobSettings->SetConnectionOptions({ "Bob", "123", senderAddress });
    bobSettings->SetFeeRate(feeRate);
    bobSettings->SetMinFeeRate(feeRate);

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetConnectionOptions({ "Alice", "123", receiverAddress });
    aliceSettings->SetFeeRate(feeRate);
    aliceSettings->SetMinFeeRate(feeRate);

    TestBitcoinWallet senderBtcWallet = GetSenderBTCWallet(*mainReactor, senderAddress, swapAmount);
    TestBitcoinWallet receiverBtcWallet = GetReceiverBTCWallet(*mainReactor, receiverAddress, swapAmount);
    auto receiverWalletDB = createReceiverWalletDB();
    auto senderWalletDB = createSenderWalletDB(0, 0);
    auto senderSP = InitSettingsProvider(senderWalletDB, bobSettings);
    auto receiverSP = InitSettingsProvider(receiverWalletDB, aliceSettings);
    auto binaryTreasury = createTreasury(senderWalletDB, kDefaultTestAmounts);
    auto sender = std::make_unique<TestWalletRig>("sender", senderWalletDB, completeAction);
    auto receiver = std::make_unique<TestWalletRig>("receiver", receiverWalletDB);

    receiverBtcWallet.addPeer(senderAddress);

    auto receiverCoins = receiver->GetCoins();
    WALLET_CHECK(receiverCoins.empty());

    io::AsyncEvent::Ptr eventToUpdate;

    bool isNeedReset = true;
    Height currentHeight = 0;
    Node node;
    TxID txID;

    eventToUpdate = io::AsyncEvent::create(*mainReactor, [&]()
    {
        if (receiver)
        {
            wallet::AtomicSwapTransaction::State txState = wallet::AtomicSwapTransaction::State::Initial;
            storage::getTxParameter(*receiver->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
            if (txState == wallet::AtomicSwapTransaction::State::Failed)
            {
                return;
            }
            else if (txState == wallet::AtomicSwapTransaction::State::HandlingContractTX && isNeedReset)
            {
                isNeedReset = false;
                currentHeight = receiver->m_WalletDB->getCurrentHeight();
                receiver.reset();
                node.m_Cfg.m_TestMode.m_FakePowSolveTime_ms = 100;
            }
        }
        else
        {
            if (sender->m_WalletDB->getCurrentHeight() - currentHeight >= 110)
            {
                receiver = std::make_unique<TestWalletRig>("receiver", receiverWalletDB, completeAction);
                InitBitcoin(receiver->m_Wallet, receiver->m_WalletDB, *mainReactor, *receiverSP);
                receiver->m_Wallet.ResumeAllTransactions();
            }
        }
        eventToUpdate->post();
    });

    eventToUpdate->post();

    NodeObserver observer([&]()
    {
        Height minHeight = 5;
        auto cursor = node.get_Processor().m_Cursor;
        if (cursor.m_Sid.m_Height == minHeight)
        {
            InitBitcoin(sender->m_Wallet, sender->m_WalletDB, *mainReactor, *senderSP);
            InitBitcoin(receiver->m_Wallet, receiver->m_WalletDB, *mainReactor, *receiverSP);

            auto parameters = InitNewSwap(receiver->m_WalletID, minHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, false);
            receiver->m_Wallet.StartTransaction(parameters);
            txID = sender->m_Wallet.StartTransaction(AcceptSwapParameters(parameters, sender->m_WalletID));
        }
    });

    InitNodeToTest(node, binaryTreasury, &observer, 32125, 2000);

    mainReactor->run();

    wallet::AtomicSwapTransaction::State txState = wallet::AtomicSwapTransaction::State::Initial;

    storage::getTxParameter(*receiver->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
    WALLET_CHECK(txState == wallet::AtomicSwapTransaction::State::Failed);

    storage::getTxParameter(*sender->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
    WALLET_CHECK(txState == wallet::AtomicSwapTransaction::State::Failed);

    auto senderCoins = sender->GetCoins();
    WALLET_CHECK(senderCoins.size() == kDefaultTestAmounts.size());

    for (size_t i = 0; i < kDefaultTestAmounts.size(); i++)
    {
        WALLET_CHECK(senderCoins[i].m_status == Coin::Available);
        WALLET_CHECK(senderCoins[i].m_ID.m_Value == kDefaultTestAmounts[i]);
    }
}

int main()
{
    int logLevel = LOG_LEVEL_DEBUG;
    auto logger = beam::Logger::create(logLevel, logLevel);
    Rules::get().FakePoW = true;
    Rules::get().UpdateChecksum();
    beam::Height fork1Height = 10;
    Rules::get().pForks[1].m_Height = fork1Height;

    TestSwapTransaction(true, fork1Height);
    TestSwapTransaction(false, fork1Height);
    TestSwapTransactionWithoutChange(true);

    TestSwapBTCRefundTransaction();
    TestSwapBeamRefundTransaction();

    ExpireByResponseTime(true);
    ExpireByResponseTime(false);

    TestSwapCancelTransaction(true, wallet::AtomicSwapTransaction::State::Initial);

    TestSwapCancelTransaction(true, wallet::AtomicSwapTransaction::State::BuildingBeamLockTX);
    TestSwapCancelTransaction(false, wallet::AtomicSwapTransaction::State::BuildingBeamLockTX);

    TestSwapCancelTransaction(true, wallet::AtomicSwapTransaction::State::BuildingBeamRedeemTX);
    TestSwapCancelTransaction(false, wallet::AtomicSwapTransaction::State::BuildingBeamRedeemTX);

    TestSwapCancelTransaction(true, wallet::AtomicSwapTransaction::State::BuildingBeamRefundTX);
    TestSwapCancelTransaction(false, wallet::AtomicSwapTransaction::State::BuildingBeamRefundTX);

    TestSwap120Blocks();

#ifndef BEAM_MAINNET
    TestElectrumSwapTransaction(true, fork1Height);
    TestElectrumSwapTransaction(false, fork1Height);

    TestElectrumSwapBTCRefundTransaction();
    TestElectrumSwapBeamRefundTransaction();
#endif // BEAM_TESTNET

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}
