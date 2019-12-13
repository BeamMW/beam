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
    const uint16_t kBtcTxMinConfirmations = 2;
    const uint32_t kLockTimeInBlocks = 100;

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

    TxParameters AcceptSwapParameters(const TxParameters& initialParameters, const WalletID& myID, Amount fee, Amount feeRate)
    {
        TxParameters parameters = initialParameters;

        parameters.SetParameter(TxParameterID::PeerID, *parameters.GetParameter<WalletID>(TxParameterID::MyID));
        parameters.SetParameter(TxParameterID::MyID, myID);

        bool isBeamSide = !*parameters.GetParameter<bool>(TxParameterID::AtomicSwapIsBeamSide);

        if (isBeamSide)
        {
            // delete parameters from other side
            parameters.DeleteParameter(TxParameterID::Fee, SubTxIndex::BEAM_REDEEM_TX);
            parameters.DeleteParameter(TxParameterID::Fee, SubTxIndex::LOCK_TX);
            parameters.DeleteParameter(TxParameterID::Fee, SubTxIndex::REFUND_TX);

            // add our parameters
            parameters.SetParameter(TxParameterID::Fee, fee, SubTxIndex::BEAM_LOCK_TX);
            parameters.SetParameter(TxParameterID::Fee, fee, SubTxIndex::BEAM_REFUND_TX);
            parameters.SetParameter(TxParameterID::Fee, feeRate, SubTxIndex::REDEEM_TX);
        }
        else
        {
            // delete parameters from other side
            parameters.DeleteParameter(TxParameterID::Fee, SubTxIndex::BEAM_LOCK_TX);
            parameters.DeleteParameter(TxParameterID::Fee, SubTxIndex::BEAM_REFUND_TX);
            parameters.DeleteParameter(TxParameterID::Fee, SubTxIndex::REDEEM_TX);

            // add our parameters
            parameters.SetParameter(TxParameterID::Fee, fee, SubTxIndex::BEAM_REDEEM_TX);
            parameters.SetParameter(TxParameterID::Fee, feeRate, SubTxIndex::LOCK_TX);
            parameters.SetParameter(TxParameterID::Fee, feeRate, SubTxIndex::REFUND_TX);
        }

        parameters.SetParameter(TxParameterID::IsSender, isBeamSide);
        parameters.SetParameter(TxParameterID::AtomicSwapIsBeamSide, isBeamSide);
        parameters.SetParameter(TxParameterID::IsInitiator, true);

        return parameters;
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
    Amount beamFee = 101;
    Amount swapAmount = 2000;
    Amount feeRate = 256;

    auto senderWalletDB = createSenderWalletDB(0, 0);
    auto binaryTreasury = createTreasury(senderWalletDB, kDefaultTestAmounts);

    auto bobSettings = std::make_shared<bitcoin::Settings>();
    bobSettings->SetConnectionOptions({ "Bob", "123", senderAddress });
    bobSettings->SetFeeRate(feeRate);
    bobSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetConnectionOptions({ "Alice", "123", receiverAddress });
    aliceSettings->SetFeeRate(feeRate);
    aliceSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    TestBitcoinWallet senderBtcWallet = GetSenderBTCWallet(*mainReactor, senderAddress, swapAmount);
    TestBitcoinWallet receiverBtcWallet = GetReceiverBTCWallet(*mainReactor, receiverAddress, swapAmount);

    auto senderSP = InitSettingsProvider(senderWalletDB, bobSettings);
    auto receiverWalletDB = createReceiverWalletDB();
    auto receiverSP = InitSettingsProvider(receiverWalletDB, aliceSettings);

    TestWalletRig sender("sender", senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);
    TestWalletRig receiver("receiver", receiverWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);

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
            bool isBeamSide = !isBeamOwnerStart;
            auto parameters = InitNewSwap(isBeamOwnerStart ? receiver.m_WalletID : sender.m_WalletID,
                currentHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, feeRate, isBeamSide);

            if (isBeamOwnerStart)
            {
                receiver.m_Wallet.StartTransaction(parameters);
                txID = sender.m_Wallet.StartTransaction(AcceptSwapParameters(parameters, sender.m_WalletID, beamFee, feeRate));
            }
            else
            {
                sender.m_Wallet.StartTransaction(parameters);
                txID = receiver.m_Wallet.StartTransaction(AcceptSwapParameters(parameters, receiver.m_WalletID, beamFee, feeRate));
            }
        }
    });

    InitNodeToTest(node, binaryTreasury, &observer, 32125, 200);

    mainReactor->run();

    WALLET_CHECK(senderSP->CanModify() == true);
    WALLET_CHECK(receiverSP->CanModify() == true);

    receiverCoins = receiver.GetCoins();
    WALLET_CHECK(receiverCoins.size() == 1);
    WALLET_CHECK(receiverCoins[0].m_ID.m_Value == beamAmount - beamFee);
    WALLET_CHECK(receiverCoins[0].m_status == Coin::Available);
    WALLET_CHECK(receiverCoins[0].m_createTxId == txID);

    auto senderCoins = sender.GetCoins();
    WALLET_CHECK(senderCoins.size() == kDefaultTestAmounts.size() + 1);
    WALLET_CHECK(senderCoins[0].m_ID.m_Value == 500);
    WALLET_CHECK(senderCoins[0].m_status == Coin::Spent);
    WALLET_CHECK(senderCoins[0].m_spentTxId == txID);
    // change
    WALLET_CHECK(senderCoins[4].m_ID.m_Value == 500 - beamAmount - beamFee);
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
    Amount beamFee = 102;
    Amount swapAmount = 200000;
    Amount feeRate = 80000;

    auto senderWalletDB = createSenderWalletDB(0, 0);
    auto binaryTreasury = createTreasury(senderWalletDB, kDefaultTestAmounts);

    auto bobSettings = std::make_shared<bitcoin::Settings>();
    bobSettings->SetElectrumConnectionOptions({ address, {"unveil", "shadow", "gold", "piece", "salad", "parent", "leisure", "obtain", "wave", "eternal", "suggest", "artwork"}, false});
    bobSettings->SetFeeRate(feeRate);
    bobSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetElectrumConnectionOptions({ address, {"rib", "genuine", "fury", "advance", "train", "capable", "rough", "silk", "march", "vague", "notice", "sphere"}, false});
    aliceSettings->SetFeeRate(feeRate);
    aliceSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    TestElectrumWallet btcWallet(*mainReactor, address);
    auto senderSP = InitSettingsProvider(senderWalletDB, bobSettings);
    auto receiverWalletDB = createReceiverWalletDB();
    auto receiverSP = InitSettingsProvider(receiverWalletDB, aliceSettings);
    TestWalletRig sender("sender", senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);
    TestWalletRig receiver("receiver", receiverWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);

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
                bool isBeamSide = !isBeamOwnerStart;
                auto parameters = InitNewSwap(isBeamOwnerStart ? receiver.m_WalletID : sender.m_WalletID,
                    currentHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, feeRate, isBeamSide);

                if (isBeamOwnerStart)
                {
                    receiver.m_Wallet.StartTransaction(parameters);
                    txID = sender.m_Wallet.StartTransaction(AcceptSwapParameters(parameters, sender.m_WalletID, beamFee, feeRate));
                }
                else
                {
                    sender.m_Wallet.StartTransaction(parameters);
                    txID = receiver.m_Wallet.StartTransaction(AcceptSwapParameters(parameters, receiver.m_WalletID, beamFee, feeRate));
                }
            }
        });

    InitNodeToTest(node, binaryTreasury, &observer, 32125, 200);

    mainReactor->run();

    receiverCoins = receiver.GetCoins();
    WALLET_CHECK(receiverCoins.size() == 1);
    WALLET_CHECK(receiverCoins[0].m_ID.m_Value == beamAmount - beamFee);
    WALLET_CHECK(receiverCoins[0].m_status == Coin::Available);
    WALLET_CHECK(receiverCoins[0].m_createTxId == txID);

    auto senderCoins = sender.GetCoins();
    WALLET_CHECK(senderCoins.size() == kDefaultTestAmounts.size() + 1);
    WALLET_CHECK(senderCoins[0].m_ID.m_Value == 500);
    WALLET_CHECK(senderCoins[0].m_status == Coin::Spent);
    WALLET_CHECK(senderCoins[0].m_spentTxId == txID);
    // change
    WALLET_CHECK(senderCoins[4].m_ID.m_Value == 500 - beamAmount - beamFee);
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

    Amount beamAmount = 380;
    Amount beamFee = 120;
    Amount swapAmount = 2000;
    Amount feeRate = 256;

    auto bobSettings = std::make_shared<bitcoin::Settings>();
    bobSettings->SetConnectionOptions({ "Bob", "123", senderAddress });
    bobSettings->SetFeeRate(feeRate);
    bobSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetConnectionOptions({ "Alice", "123", receiverAddress });
    aliceSettings->SetFeeRate(feeRate);
    aliceSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    TestBitcoinWallet senderBtcWallet = GetSenderBTCWallet(*mainReactor, senderAddress, swapAmount);
    TestBitcoinWallet receiverBtcWallet = GetReceiverBTCWallet(*mainReactor, receiverAddress, swapAmount);
    auto senderWalletDB = createSenderWalletDB(false, kDefaultTestAmounts);
    auto senderSP = InitSettingsProvider(senderWalletDB, bobSettings);
    auto receiverWalletDB = createReceiverWalletDB();
    auto receiverSP = InitSettingsProvider(receiverWalletDB, aliceSettings);
    TestWalletRig sender("sender", senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);
    TestWalletRig receiver("receiver", receiverWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);

    InitBitcoin(sender.m_Wallet, sender.m_WalletDB, *mainReactor, *senderSP);
    InitBitcoin(receiver.m_Wallet, receiver.m_WalletDB, *mainReactor, *receiverSP);

    receiverBtcWallet.addPeer(senderAddress);

    TestNode node{ TestNode::NewBlockFunc(), kNodeStartHeight };
    Height currentHeight = node.m_Blockchain.m_mcm.m_vStates.size();
    TxID txID = { {0} };

    if (isBeamOwnerStart)
    {
        auto parameters = InitNewSwap(receiver.m_WalletID, currentHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, feeRate, false);
        receiver.m_Wallet.StartTransaction(parameters);
        txID = sender.m_Wallet.StartTransaction(AcceptSwapParameters(parameters, sender.m_WalletID, beamFee, feeRate));
    }
    else
    {
        auto parameters = InitNewSwap(sender.m_WalletID, currentHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, feeRate, true);
        sender.m_Wallet.StartTransaction(parameters);
        txID = receiver.m_Wallet.StartTransaction(AcceptSwapParameters(parameters, receiver.m_WalletID, beamFee, feeRate));
    }

    auto receiverCoins = receiver.GetCoins();
    WALLET_CHECK(receiverCoins.empty());

    io::Timer::Ptr timer = io::Timer::create(*mainReactor);
    timer->start(200, true, [&node]() {node.AddBlock(); });

    mainReactor->run();

    receiverCoins = receiver.GetCoins();
    WALLET_CHECK(receiverCoins.size() == 1);
    WALLET_CHECK(receiverCoins[0].m_ID.m_Value == beamAmount - beamFee);
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

    auto bobSettings = std::make_shared<bitcoin::Settings>();
    bobSettings->SetConnectionOptions({ "Bob", "123", senderAddress });
    bobSettings->SetFeeRate(feeRate);
    bobSettings->SetLockTimeInBlocks(kLockTimeInBlocks);
    bobSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetConnectionOptions({ "Alice", "123", receiverAddress });
    aliceSettings->SetFeeRate(feeRate);
    aliceSettings->SetLockTimeInBlocks(kLockTimeInBlocks);
    aliceSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    TestBitcoinWallet senderBtcWallet = GetSenderBTCWallet(*mainReactor, senderAddress, swapAmount);
    TestBitcoinWallet receiverBtcWallet = GetReceiverBTCWallet(*mainReactor, receiverAddress, swapAmount);
    receiverBtcWallet.addPeer(senderAddress);

    auto senderWalletDB = createSenderWalletDB(false, kDefaultTestAmounts);
    auto senderSP = InitSettingsProvider(senderWalletDB, bobSettings);
    auto receiverWalletDB = createReceiverWalletDB();
    auto receiverSP = InitSettingsProvider(receiverWalletDB, aliceSettings);
    auto sender = std::make_unique<TestWalletRig>("sender", senderWalletDB, completedAction, TestWalletRig::RegularWithoutPoWBbs);
    auto receiver = std::make_shared<TestWalletRig>("receiver", receiverWalletDB, completedAction, TestWalletRig::RegularWithoutPoWBbs);

    InitBitcoin(sender->m_Wallet, sender->m_WalletDB, *mainReactor, *senderSP);
    InitBitcoin(receiver->m_Wallet, receiver->m_WalletDB, *mainReactor, *receiverSP);

    TestNode node{ TestNode::NewBlockFunc(), kNodeStartHeight };
    Height currentHeight = node.m_Blockchain.m_mcm.m_vStates.size();

    auto parameters = InitNewSwap(receiver->m_WalletID, currentHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, feeRate, false);
    receiver->m_Wallet.StartTransaction(parameters);
    TxID txID = sender->m_Wallet.StartTransaction(AcceptSwapParameters(parameters, sender->m_WalletID, beamFee, feeRate));

    auto receiverCoins = receiver->GetCoins();
    WALLET_CHECK(receiverCoins.empty());

    io::Timer::Ptr timer = io::Timer::create(*mainReactor);
    timer->start(200, true, [&node]() {node.AddBlock(); });

    io::AsyncEvent::Ptr eventToUpdate;
    uint64_t startBlocks = receiverBtcWallet.getBlockCount();

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
    WALLET_CHECK(receiverBtcWallet.getBlockCount() - startBlocks >= kLockTimeInBlocks);

    // TODO: add check BTC balance
}

void TestSwapBTCQuickRefundTransaction()
{
    cout << "\nAtomic swap: testing BTC quick refund transaction...\n";

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
    bobSettings->SetLockTimeInBlocks(kLockTimeInBlocks);
    bobSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetConnectionOptions({ "Alice", "123", receiverAddress });
    aliceSettings->SetFeeRate(feeRate);
    aliceSettings->SetLockTimeInBlocks(kLockTimeInBlocks);
    aliceSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    TestBitcoinWallet senderBtcWallet = GetSenderBTCWallet(*mainReactor, senderAddress, swapAmount);
    TestBitcoinWallet receiverBtcWallet = GetReceiverBTCWallet(*mainReactor, receiverAddress, swapAmount);
    receiverBtcWallet.addPeer(senderAddress);

    auto senderWalletDB = createSenderWalletDB(false, kDefaultTestAmounts);
    auto senderSP = InitSettingsProvider(senderWalletDB, bobSettings);
    auto receiverWalletDB = createReceiverWalletDB();
    auto receiverSP = InitSettingsProvider(receiverWalletDB, aliceSettings);
    auto sender = std::make_unique<TestWalletRig>("sender", senderWalletDB, Wallet::TxCompletedAction(), TestWalletRig::RegularWithoutPoWBbs);
    auto receiver = std::make_shared<TestWalletRig>("receiver", receiverWalletDB, completedAction, TestWalletRig::RegularWithoutPoWBbs);

    InitBitcoin(sender->m_Wallet, sender->m_WalletDB, *mainReactor, *senderSP);
    InitBitcoin(receiver->m_Wallet, receiver->m_WalletDB, *mainReactor, *receiverSP);

    TestNode node{ TestNode::NewBlockFunc(), kNodeStartHeight };
    Height currentHeight = node.m_Blockchain.m_mcm.m_vStates.size();

    auto parameters = InitNewSwap(receiver->m_WalletID, currentHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, feeRate, false);
    receiver->m_Wallet.StartTransaction(parameters);
    TxID txID = sender->m_Wallet.StartTransaction(AcceptSwapParameters(parameters, sender->m_WalletID, beamFee, feeRate));

    auto receiverCoins = receiver->GetCoins();
    WALLET_CHECK(receiverCoins.empty());

    io::Timer::Ptr timer = io::Timer::create(*mainReactor);
    timer->start(200, true, [&node]() {node.AddBlock(); });

    io::AsyncEvent::Ptr eventToUpdate;
    bool isCanceled = false;
    uint64_t startBlocks = receiverBtcWallet.getBlockCount();

    eventToUpdate = io::AsyncEvent::create(*mainReactor, [&sender, receiver, txID, &eventToUpdate, &timer, &isCanceled]()
    {
        if (!isCanceled)
        {
            wallet::AtomicSwapTransaction::State txState = wallet::AtomicSwapTransaction::State::Initial;
            storage::getTxParameter(*sender->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
            if (txState == wallet::AtomicSwapTransaction::State::HandlingContractTX)
            {
                sender->m_Wallet.CancelTransaction(txID);
                isCanceled = true;
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
    WALLET_CHECK(receiverBtcWallet.getBlockCount() - startBlocks < kLockTimeInBlocks);
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

    auto bobSettings = std::make_shared<bitcoin::Settings>();
    bobSettings->SetElectrumConnectionOptions({ address, {"unveil", "shadow", "gold", "piece", "salad", "parent", "leisure", "obtain", "wave", "eternal", "suggest", "artwork"}, false});
    bobSettings->SetFeeRate(feeRate);
    bobSettings->SetLockTimeInBlocks(kLockTimeInBlocks);
    bobSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetElectrumConnectionOptions({ address, {"rib", "genuine", "fury", "advance", "train", "capable", "rough", "silk", "march", "vague", "notice", "sphere"}, false});
    aliceSettings->SetFeeRate(feeRate);
    aliceSettings->SetLockTimeInBlocks(kLockTimeInBlocks);
    aliceSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    auto senderWalletDB = createSenderWalletDB(false, kDefaultTestAmounts);
    auto senderSP = InitSettingsProvider(senderWalletDB, bobSettings);
    auto receiverWalletDB = createReceiverWalletDB();
    auto receiverSP = InitSettingsProvider(receiverWalletDB, aliceSettings);
    TestElectrumWallet btcWallet(*mainReactor, address);
    auto sender = std::make_unique<TestWalletRig>("sender", senderWalletDB, completedAction, TestWalletRig::RegularWithoutPoWBbs);
    auto receiver = std::make_shared<TestWalletRig>("receiver", receiverWalletDB, completedAction, TestWalletRig::RegularWithoutPoWBbs);

    InitElectrum(sender->m_Wallet, sender->m_WalletDB, *mainReactor, *senderSP);
    InitElectrum(receiver->m_Wallet, receiver->m_WalletDB, *mainReactor, *receiverSP);

    TestNode node{ TestNode::NewBlockFunc(), kNodeStartHeight };
    Height currentHeight = node.m_Blockchain.m_mcm.m_vStates.size();
    auto parameters = InitNewSwap(receiver->m_WalletID, currentHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, feeRate, false);

    receiver->m_Wallet.StartTransaction(parameters);
    TxID txID = sender->m_Wallet.StartTransaction(AcceptSwapParameters(parameters, sender->m_WalletID, beamFee, feeRate));

    auto receiverCoins = receiver->GetCoins();
    WALLET_CHECK(receiverCoins.empty());

    io::Timer::Ptr timer = io::Timer::create(*mainReactor);
    timer->start(200, true, [&node]() {node.AddBlock(); });

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

    Amount beamAmount = 350;
    Amount beamFee = 125;
    Amount swapAmount = 2000;
    Amount feeRate = 256;

    auto bobSettings = std::make_shared<bitcoin::Settings>();
    bobSettings->SetConnectionOptions({ "Bob", "123", senderAddress });
    bobSettings->SetFeeRate(feeRate);
    bobSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetConnectionOptions({ "Alice", "123", receiverAddress });
    aliceSettings->SetFeeRate(feeRate);
    aliceSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    TestBitcoinWallet senderBtcWallet = GetSenderBTCWallet(*mainReactor, senderAddress, swapAmount);
    TestBitcoinWallet receiverBtcWallet = GetReceiverBTCWallet(*mainReactor, receiverAddress, swapAmount);
    receiverBtcWallet.addPeer(senderAddress);

    auto senderWalletDB = createSenderWalletDB(false, kDefaultTestAmounts);
    auto senderSP = InitSettingsProvider(senderWalletDB, bobSettings);
    auto receiverWalletDB = createReceiverWalletDB();
    auto receiverSP = InitSettingsProvider(receiverWalletDB, aliceSettings);
    auto sender = std::make_unique<TestWalletRig>("sender", senderWalletDB, completedAction, TestWalletRig::RegularWithoutPoWBbs);
    auto receiver = std::make_unique<TestWalletRig>("receiver", receiverWalletDB, completedAction, TestWalletRig::RegularWithoutPoWBbs);

    InitBitcoin(sender->m_Wallet, sender->m_WalletDB, *mainReactor, *senderSP);
    InitBitcoin(receiver->m_Wallet, receiver->m_WalletDB, *mainReactor, *receiverSP);

    TestNode node{ TestNode::NewBlockFunc(), kNodeStartHeight };
    Height currentHeight = node.m_Blockchain.m_mcm.m_vStates.size();
    auto parameters = InitNewSwap(receiver->m_WalletID, currentHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, feeRate, false);
    receiver->m_Wallet.StartTransaction(parameters);
    TxID txID = sender->m_Wallet.StartTransaction(AcceptSwapParameters(parameters, sender->m_WalletID, beamFee, feeRate));

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
    timer->start(200, true, [&node]() {node.AddBlock(); });

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
    WALLET_CHECK(senderCoins[4].m_ID.m_Value == 500 - beamAmount - beamFee);
    WALLET_CHECK(senderCoins[4].m_status == Coin::Available);
    WALLET_CHECK(senderCoins[4].m_createTxId == txID);

    // Refund
    WALLET_CHECK(senderCoins[5].m_ID.m_Value == beamAmount - beamFee);
    WALLET_CHECK(senderCoins[5].m_status == Coin::Available);
    WALLET_CHECK(senderCoins[5].m_createTxId == txID);
}

void TestSwapBeamAndBTCRefundTransaction()
{
    cout << "\nAtomic swap: testing Beam and BTC refund transactions...\n";

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    int completedCount = 2;
    auto completedAction = [&completedCount, mainReactor](auto)
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

    Amount beamAmount = 350;
    Amount beamFee = 125;
    Amount swapAmount = 2000;
    Amount feeRate = 256;

    auto bobSettings = std::make_shared<bitcoin::Settings>();
    bobSettings->SetConnectionOptions({ "Bob", "123", senderAddress });
    bobSettings->SetFeeRate(feeRate);
    bobSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetConnectionOptions({ "Alice", "123", receiverAddress });
    aliceSettings->SetFeeRate(feeRate);
    aliceSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    TestBitcoinWallet senderBtcWallet = GetSenderBTCWallet(*mainReactor, senderAddress, swapAmount);
    TestBitcoinWallet receiverBtcWallet = GetReceiverBTCWallet(*mainReactor, receiverAddress, swapAmount);
    receiverBtcWallet.addPeer(senderAddress);

    auto senderWalletDB = createSenderWalletDB(0, 0);
    auto senderSP = InitSettingsProvider(senderWalletDB, bobSettings);
    auto receiverWalletDB = createReceiverWalletDB();
    auto receiverSP = InitSettingsProvider(receiverWalletDB, aliceSettings);
    auto binaryTreasury = createTreasury(senderWalletDB, kDefaultTestAmounts);
    auto sender = std::make_unique<TestWalletRig>("sender", senderWalletDB, completedAction, TestWalletRig::RegularWithoutPoWBbs);
    auto receiver = std::make_unique<TestWalletRig>("receiver", receiverWalletDB, completedAction, TestWalletRig::RegularWithoutPoWBbs);

    vector<Coin> receiverCoins;
    io::AsyncEvent::Ptr eventToUpdate;
    bool isNeedReset = true;
    Node node;
    TxID txID;

    eventToUpdate = io::AsyncEvent::create(*mainReactor, [&]()
    {
        if (receiver && isNeedReset)
        {
            wallet::AtomicSwapTransaction::State txState = wallet::AtomicSwapTransaction::State::Initial;
            storage::getTxParameter(*receiver->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
            if (txState == wallet::AtomicSwapTransaction::State::SendingBeamRedeemTX)
            {
                // delete receiver to simulate refund on Beam side
                receiver.reset();
                isNeedReset = false;
                node.m_Cfg.m_TestMode.m_FakePowSolveTime_ms = 50;
            }
        }
        else
        {
            Height minHeight;
            storage::getTxParameter(*sender->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::MinHeight, minHeight);
            Height currentHeight = sender->m_WalletDB->getCurrentHeight();
            
            if (currentHeight - minHeight > 5 * 60 && !receiver)
            {
                receiver = std::make_unique<TestWalletRig>("receiver", receiverWalletDB, completedAction, TestWalletRig::RegularWithoutPoWBbs);
                InitBitcoin(receiver->m_Wallet, receiver->m_WalletDB, *mainReactor, *receiverSP);
                receiver->m_Wallet.ResumeAllTransactions();
            }
        }
        eventToUpdate->post();
    });


    NodeObserver observer([&]()
    {
        Height minHeight = 15;
        auto cursor = node.get_Processor().m_Cursor;
        if (cursor.m_Sid.m_Height == minHeight)
        {
            InitBitcoin(sender->m_Wallet, sender->m_WalletDB, *mainReactor, *senderSP);
            InitBitcoin(receiver->m_Wallet, receiver->m_WalletDB, *mainReactor, *receiverSP);

            auto parameters = InitNewSwap(receiver->m_WalletID, minHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, feeRate, false);
            receiver->m_Wallet.StartTransaction(parameters);
            txID = sender->m_Wallet.StartTransaction(AcceptSwapParameters(parameters, sender->m_WalletID, beamFee, feeRate));
            auto receiverCoins = receiver->GetCoins();
            WALLET_CHECK(receiverCoins.empty());
        }
    });

    InitNodeToTest(node, binaryTreasury, &observer, 32125, 500);


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
    WALLET_CHECK(senderCoins[4].m_ID.m_Value == 500 - beamAmount - beamFee);
    WALLET_CHECK(senderCoins[4].m_status == Coin::Available);
    WALLET_CHECK(senderCoins[4].m_createTxId == txID);

    // Refund
    WALLET_CHECK(senderCoins[5].m_ID.m_Value == beamAmount - beamFee);
    WALLET_CHECK(senderCoins[5].m_status == Coin::Available);
    WALLET_CHECK(senderCoins[5].m_createTxId == txID);

    txState = wallet::AtomicSwapTransaction::State::Initial;
    storage::getTxParameter(*receiver->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
    WALLET_CHECK(txState == wallet::AtomicSwapTransaction::State::Refunded);
}

void TestSwapBTCRedeemAfterExpired()
{
    cout << "\nAtomic swap: testing BTC redeem after Beam expired...\n";

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    int completedCount = 2;
    auto completedAction = [&completedCount, mainReactor](auto)
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

    Amount beamAmount = 350;
    Amount beamFee = 125;
    Amount swapAmount = 2000;
    Amount feeRate = 256;

    auto bobSettings = std::make_shared<bitcoin::Settings>();
    bobSettings->SetConnectionOptions({ "Bob", "123", senderAddress });
    bobSettings->SetFeeRate(feeRate);
    bobSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetConnectionOptions({ "Alice", "123", receiverAddress });
    aliceSettings->SetFeeRate(feeRate);
    aliceSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    TestBitcoinWallet senderBtcWallet = GetSenderBTCWallet(*mainReactor, senderAddress, swapAmount);
    TestBitcoinWallet receiverBtcWallet = GetReceiverBTCWallet(*mainReactor, receiverAddress, swapAmount);
    receiverBtcWallet.addPeer(senderAddress);

    auto senderWalletDB = createSenderWalletDB(0, 0);
    auto senderSP = InitSettingsProvider(senderWalletDB, bobSettings);
    auto receiverWalletDB = createReceiverWalletDB();
    auto receiverSP = InitSettingsProvider(receiverWalletDB, aliceSettings);
    auto binaryTreasury = createTreasury(senderWalletDB, kDefaultTestAmounts);
    auto sender = std::make_unique<TestWalletRig>("sender", senderWalletDB, completedAction, TestWalletRig::RegularWithoutPoWBbs);
    auto receiver = std::make_unique<TestWalletRig>("receiver", receiverWalletDB, completedAction, TestWalletRig::RegularWithoutPoWBbs);

    vector<Coin> receiverCoins;
    io::AsyncEvent::Ptr eventToUpdate;
    bool isNeedReset = true;
    Node node;
    TxID txID;

    eventToUpdate = io::AsyncEvent::create(*mainReactor, [&]()
    {
        if (sender && isNeedReset)
        {
            wallet::AtomicSwapTransaction::State txState = wallet::AtomicSwapTransaction::State::Initial;
            storage::getTxParameter(*sender->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
            if (txState == wallet::AtomicSwapTransaction::State::SendingBeamRedeemTX)
            {
                sender.reset();
                isNeedReset = false;
                node.m_Cfg.m_TestMode.m_FakePowSolveTime_ms = 50;
            }
        }
        else
        {
            Height minHeight;
            storage::getTxParameter(*receiver->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::MinHeight, minHeight);
            Height currentHeight = receiver->m_WalletDB->getCurrentHeight();

            if (currentHeight - minHeight > 6 * 60 && !sender)
            {
                sender = std::make_unique<TestWalletRig>("sender", senderWalletDB, completedAction, TestWalletRig::RegularWithoutPoWBbs);
                InitBitcoin(sender->m_Wallet, sender->m_WalletDB, *mainReactor, *senderSP);
                sender->m_Wallet.ResumeAllTransactions();
            }

            if (currentHeight - minHeight > 500)
            {
                mainReactor->stop();
            }
        }
        eventToUpdate->post();
    });


    NodeObserver observer([&]()
    {
        Height minHeight = 15;
        auto cursor = node.get_Processor().m_Cursor;
        if (cursor.m_Sid.m_Height == minHeight)
        {
            InitBitcoin(sender->m_Wallet, sender->m_WalletDB, *mainReactor, *senderSP);
            InitBitcoin(receiver->m_Wallet, receiver->m_WalletDB, *mainReactor, *receiverSP);

            auto parameters = InitNewSwap(receiver->m_WalletID, minHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, feeRate, false);
            receiver->m_Wallet.StartTransaction(parameters);
            txID = sender->m_Wallet.StartTransaction(AcceptSwapParameters(parameters, sender->m_WalletID, beamFee, feeRate));
            auto receiverCoins = receiver->GetCoins();
            WALLET_CHECK(receiverCoins.empty());
        }
    });

    InitNodeToTest(node, binaryTreasury, &observer, 32125, 500);


    eventToUpdate->post();
    mainReactor->run();

    // validate sender TX state
    wallet::AtomicSwapTransaction::State txState = wallet::AtomicSwapTransaction::State::Initial;
    storage::getTxParameter(*sender->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
    WALLET_CHECK(txState == wallet::AtomicSwapTransaction::State::CompleteSwap);

    txState = wallet::AtomicSwapTransaction::State::Initial;
    storage::getTxParameter(*receiver->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
    WALLET_CHECK(txState == wallet::AtomicSwapTransaction::State::CompleteSwap);
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

    Amount beamAmount = 320;
    Amount beamFee = 110;
    Amount swapAmount = 200000;
    Amount feeRate = 80000;

    auto bobSettings = std::make_shared<bitcoin::Settings>();
    bobSettings->SetElectrumConnectionOptions({ address, {"unveil", "shadow", "gold", "piece", "salad", "parent", "leisure", "obtain", "wave", "eternal", "suggest", "artwork"}, false});
    bobSettings->SetFeeRate(feeRate);
    bobSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetElectrumConnectionOptions({ address, {"rib", "genuine", "fury", "advance", "train", "capable", "rough", "silk", "march", "vague", "notice", "sphere"}, false});
    aliceSettings->SetFeeRate(feeRate);
    aliceSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    TestElectrumWallet btcWallet(*mainReactor, address);
    auto senderWalletDB = createSenderWalletDB(false, kDefaultTestAmounts);
    auto senderSP = InitSettingsProvider(senderWalletDB, bobSettings);
    auto receiverWalletDB = createReceiverWalletDB();
    auto receiverSP = InitSettingsProvider(receiverWalletDB, aliceSettings);
    auto sender = std::make_unique<TestWalletRig>("sender", senderWalletDB, completedAction, TestWalletRig::RegularWithoutPoWBbs);
    auto receiver = std::make_unique<TestWalletRig>("receiver", receiverWalletDB, completedAction, TestWalletRig::RegularWithoutPoWBbs);

    InitElectrum(sender->m_Wallet, sender->m_WalletDB, *mainReactor, *senderSP);
    InitElectrum(receiver->m_Wallet, receiver->m_WalletDB, *mainReactor, *receiverSP);

    TestNode node{ TestNode::NewBlockFunc(), kNodeStartHeight };
    Height currentHeight = node.m_Blockchain.m_mcm.m_vStates.size();
    auto parameters = InitNewSwap(receiver->m_WalletID, currentHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, feeRate, false);
    receiver->m_Wallet.StartTransaction(parameters);
    TxID txID = sender->m_Wallet.StartTransaction(AcceptSwapParameters(parameters, sender->m_WalletID, beamFee, feeRate));

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
    timer->start(200, true, [&node]() {node.AddBlock(); });

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
    WALLET_CHECK(senderCoins[4].m_ID.m_Value == 500 - beamAmount - beamFee);
    WALLET_CHECK(senderCoins[4].m_status == Coin::Available);
    WALLET_CHECK(senderCoins[4].m_createTxId == txID);

    // Refund
    WALLET_CHECK(senderCoins[5].m_ID.m_Value == beamAmount - beamFee);
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
    auto swapParameters = InitNewSwap(receiverWalletID, currentHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, feeRate, !isBeamSide, lifetime, responseTime);
    TxID txID = sender->m_Wallet.StartTransaction(AcceptSwapParameters(swapParameters, sender->m_WalletID, beamFee, feeRate));
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
    bobSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetConnectionOptions({ "Alice", "123", receiverAddress });
    aliceSettings->SetFeeRate(feeRate);
    aliceSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

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
    auto parameters = InitNewSwap(receiver->m_WalletID, currentHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, feeRate, false);
    receiver->m_Wallet.StartTransaction(parameters);
    TxID txID = sender->m_Wallet.StartTransaction(AcceptSwapParameters(parameters, sender->m_WalletID, beamFee, feeRate));

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

void TestExpireByLifeTime()
{
    cout << "\nAtomic swap: expire by lifetime ...\n";

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
    bobSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetConnectionOptions({ "Alice", "123", receiverAddress });
    aliceSettings->SetFeeRate(feeRate);
    aliceSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    TestBitcoinWallet senderBtcWallet = GetSenderBTCWallet(*mainReactor, senderAddress, swapAmount);
    TestBitcoinWallet receiverBtcWallet = GetReceiverBTCWallet(*mainReactor, receiverAddress, swapAmount);
    auto receiverWalletDB = createReceiverWalletDB();
    auto senderWalletDB = createSenderWalletDB(0, 0);
    auto senderSP = InitSettingsProvider(senderWalletDB, bobSettings);
    auto receiverSP = InitSettingsProvider(receiverWalletDB, aliceSettings);
    auto binaryTreasury = createTreasury(senderWalletDB, kDefaultTestAmounts);
    auto sender = std::make_unique<TestWalletRig>("sender", senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);
    auto receiver = std::make_unique<TestWalletRig>("receiver", receiverWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);

    receiverBtcWallet.addPeer(senderAddress);

    auto receiverCoins = receiver->GetCoins();
    WALLET_CHECK(receiverCoins.empty());

    bool isNeedReset = true;
    io::AsyncEvent::Ptr eventToUpdate;
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
                if (txState == wallet::AtomicSwapTransaction::State::BuildingBeamLockTX && isNeedReset)
                {
                    receiver.reset();
                    isNeedReset = false;
                }
            }
            else if (sender)
            {
                wallet::AtomicSwapTransaction::State txState = wallet::AtomicSwapTransaction::State::Initial;
                storage::getTxParameter(*sender->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
                if (txState == wallet::AtomicSwapTransaction::State::Failed)
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

                auto parameters = InitNewSwap(receiver->m_WalletID, minHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, feeRate, false);
                receiver->m_Wallet.StartTransaction(parameters);
                txID = sender->m_Wallet.StartTransaction(AcceptSwapParameters(parameters, sender->m_WalletID, beamFee, feeRate));
            }
        });

    InitNodeToTest(node, binaryTreasury, &observer, 32125, 200);

    mainReactor->run();

    wallet::AtomicSwapTransaction::State txState = wallet::AtomicSwapTransaction::State::Initial;

    storage::getTxParameter(*receiver->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
    WALLET_CHECK(txState == wallet::AtomicSwapTransaction::State::Failed);

    storage::getTxParameter(*sender->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
    WALLET_CHECK(txState == wallet::AtomicSwapTransaction::State::Failed);

    TxFailureReason reason = TxFailureReason::Unknown;
    storage::getTxParameter(*sender->m_WalletDB, txID, wallet::kDefaultSubTxID, TxParameterID::InternalFailureReason, reason);
    WALLET_CHECK(reason == TxFailureReason::TransactionExpired);

    reason = TxFailureReason::Unknown;
    storage::getTxParameter(*receiver->m_WalletDB, txID, wallet::kDefaultSubTxID, TxParameterID::InternalFailureReason, reason);
    WALLET_CHECK(reason == TxFailureReason::TransactionExpired);

    auto senderCoins = sender->GetCoins();
    WALLET_CHECK(senderCoins.size() == kDefaultTestAmounts.size());

    for (size_t i = 0; i < kDefaultTestAmounts.size(); i++)
    {
        WALLET_CHECK(senderCoins[i].m_status == Coin::Available);
        WALLET_CHECK(senderCoins[i].m_ID.m_Value == kDefaultTestAmounts[i]);
    }
}

void TestIgnoringThirdPeer()
{
    cout << "\nAtomic swap: testing ignoring of third peer\n";

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    int completedCount = 2;
    auto completedAction = [&completedCount, mainReactor](auto)
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
    bobSettings->SetLockTimeInBlocks(kLockTimeInBlocks);
    bobSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    auto aliceSettings = std::make_shared<bitcoin::Settings>();
    aliceSettings->SetConnectionOptions({ "Alice", "123", receiverAddress });
    aliceSettings->SetFeeRate(feeRate);
    aliceSettings->SetLockTimeInBlocks(kLockTimeInBlocks);
    aliceSettings->SetTxMinConfirmations(kBtcTxMinConfirmations);

    TestBitcoinWallet senderBtcWallet = GetSenderBTCWallet(*mainReactor, senderAddress, swapAmount);
    TestBitcoinWallet receiverBtcWallet = GetReceiverBTCWallet(*mainReactor, receiverAddress, swapAmount);
    receiverBtcWallet.addPeer(senderAddress);

    auto senderWalletDB = createSenderWalletDB(false, kDefaultTestAmounts);
    auto senderSP = InitSettingsProvider(senderWalletDB, bobSettings);
    auto receiverWalletDB = createReceiverWalletDB();
    auto receiverSP = InitSettingsProvider(receiverWalletDB, aliceSettings);
    auto sender = std::make_unique<TestWalletRig>("sender", senderWalletDB, completedAction, TestWalletRig::RegularWithoutPoWBbs);
    auto receiver = std::make_shared<TestWalletRig>("receiver", receiverWalletDB, completedAction, TestWalletRig::RegularWithoutPoWBbs);

    InitBitcoin(sender->m_Wallet, sender->m_WalletDB, *mainReactor, *senderSP);
    InitBitcoin(receiver->m_Wallet, receiver->m_WalletDB, *mainReactor, *receiverSP);

    TestNode node{ TestNode::NewBlockFunc(), kNodeStartHeight };
    Height currentHeight = node.m_Blockchain.m_mcm.m_vStates.size();

    auto parameters = InitNewSwap(receiver->m_WalletID, currentHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, feeRate, false);
    receiver->m_Wallet.StartTransaction(parameters);
    TxID txID = sender->m_Wallet.StartTransaction(AcceptSwapParameters(parameters, sender->m_WalletID, beamFee, feeRate));

    auto receiverCoins = receiver->GetCoins();
    WALLET_CHECK(receiverCoins.empty());

    io::Timer::Ptr timer = io::Timer::create(*mainReactor);
    timer->start(200, true, [&node]() {node.AddBlock(); });

    io::AsyncEvent::Ptr eventToUpdate;

    eventToUpdate = io::AsyncEvent::create(*mainReactor, [&]()
    {
        WalletID peerID;
        bool result = storage::getTxParameter(*receiver->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::PeerID, peerID);
        if (result)
        {
            wallet::AtomicSwapTransaction::State txState = wallet::AtomicSwapTransaction::State::Initial;
            storage::getTxParameter(*sender->m_WalletDB, txID, wallet::kDefaultSubTxID, wallet::TxParameterID::State, txState);
            if (txState == wallet::AtomicSwapTransaction::State::BuildingBeamLockTX)
            {
                // create new address
                WalletAddress newAddress = storage::createAddress(*sender->m_WalletDB, sender->m_KeyKeeper);
                sender->m_WalletDB->saveAddress(newAddress);

                // send msg from new address
                SetTxParameter msg;
                msg.AddParameter(TxParameterID::SubTxIndex, SubTxIndex::BEAM_REFUND_TX)
                    .AddParameter(TxParameterID::PeerSignature, ECC::Scalar::Native());

                msg.m_TxID = txID;
                msg.m_Type = wallet::TxType::AtomicSwap;
                msg.m_From = newAddress.m_walletID;

                sender->m_messageEndpoint->Send(receiver->m_WalletID, msg);
                return;
            }
        }
        eventToUpdate->post();
    });

    eventToUpdate->post();
    mainReactor->run();

    WALLET_CHECK(senderSP->CanModify() == true);
    WALLET_CHECK(receiverSP->CanModify() == true);

    receiverCoins = receiver->GetCoins();
    WALLET_CHECK(receiverCoins.size() == 1);
    WALLET_CHECK(receiverCoins[0].m_ID.m_Value == beamAmount - beamFee);
    WALLET_CHECK(receiverCoins[0].m_status == Coin::Available);
    WALLET_CHECK(receiverCoins[0].m_createTxId == txID);

    auto senderCoins = sender->GetCoins();
    WALLET_CHECK(senderCoins.size() == kDefaultTestAmounts.size() + 1);
    WALLET_CHECK(senderCoins[0].m_ID.m_Value == 500);
    WALLET_CHECK(senderCoins[0].m_status == Coin::Spent);
    WALLET_CHECK(senderCoins[0].m_spentTxId == txID);
    // change
    WALLET_CHECK(senderCoins[4].m_ID.m_Value == 500 - beamAmount - beamFee);
    WALLET_CHECK(senderCoins[4].m_status == Coin::Available);
    WALLET_CHECK(senderCoins[4].m_createTxId == txID);
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

    TestSwapBTCQuickRefundTransaction();

    TestSwapBTCRefundTransaction();
    TestSwapBeamRefundTransaction();
    TestSwapBeamAndBTCRefundTransaction();
    TestSwapBTCRedeemAfterExpired();

    ExpireByResponseTime(true);
    ExpireByResponseTime(false);
    TestExpireByLifeTime();

    TestSwapCancelTransaction(true, wallet::AtomicSwapTransaction::State::Initial);

    TestSwapCancelTransaction(true, wallet::AtomicSwapTransaction::State::BuildingBeamLockTX);
    TestSwapCancelTransaction(false, wallet::AtomicSwapTransaction::State::BuildingBeamLockTX);

    TestSwapCancelTransaction(true, wallet::AtomicSwapTransaction::State::BuildingBeamRedeemTX);
    TestSwapCancelTransaction(false, wallet::AtomicSwapTransaction::State::BuildingBeamRedeemTX);

    TestSwapCancelTransaction(true, wallet::AtomicSwapTransaction::State::BuildingBeamRefundTX);
    TestSwapCancelTransaction(false, wallet::AtomicSwapTransaction::State::BuildingBeamRefundTX);


#ifndef BEAM_MAINNET
    TestElectrumSwapTransaction(true, fork1Height);
    TestElectrumSwapTransaction(false, fork1Height);

    TestElectrumSwapBTCRefundTransaction();
    TestElectrumSwapBeamRefundTransaction();
#endif // BEAM_TESTNET

    TestIgnoringThirdPeer();

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}
