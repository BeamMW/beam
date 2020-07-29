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
#include <boost/filesystem.hpp>
#include "utility/logger.h"
#include "keykeeper/local_private_key_keeper.h"
#include "wallet/core/common.h"
#include "wallet/core/wallet_network.h"
#include "wallet/core/wallet.h"
#include "wallet/core/base_transaction.h"
#include "wallet/core/simple_transaction.h"
#include "wallet/transactions/assets/assets_reg_creators.h"
#include "node/node.h"
#include "core/radixtree.h"
#include "core/unittest/mini_blockchain.h"
#include "utility/test_helpers.h"
#include "test_helpers.h"
WALLET_TEST_INIT
#include "wallet_test_environment.cpp"

void TestAssets() {
    //
    // Assets issue
    //
    LOG_INFO() << "\nTesting assets...";

    beam::io::Reactor::Ptr mainReactor{beam::io::Reactor::create()};
    beam::io::Reactor::Scope scope(*mainReactor);

    int waitCount = 0;
    const auto stopReactor = [&waitCount, mainReactor](auto)
    {
        --waitCount;
        if (waitCount == 0)
        {
            mainReactor->stop();
        }
    };

    TestNode node;
    const auto walletDB = createSqliteWalletDB("sender_wallet.db", false, true);
    walletDB->AllocateKidRange(100500);

    TestWalletRig sender(walletDB, stopReactor);
    WALLET_CHECK(sender.m_WalletDB->getTxHistory().empty());

    WALLET_CHECK(node.GetHeight() > Rules::get().pForks[1].m_Height);
    WALLET_CHECK(node.GetHeight() > Rules::get().pForks[2].m_Height);
    beam::wallet::RegisterAssetCreators(sender.m_Wallet);

    //const Key::Index assetOwnerIdx = Key::Index(22);

    Asset::ID assetId = 445; // whatever

    const auto checkTotals = [&] (Amount beam, Amount amountAsset) {
        storage::Totals allTotals(*walletDB);
        WALLET_CHECK(allTotals.GetBeamTotals().Avail == AmountBig::Type(beam));
        WALLET_CHECK(allTotals.GetTotals(assetId).Avail == AmountBig::Type(amountAsset));
    };

    const auto makeCoin = [&walletDB](Amount amount) {
        auto coin = CreateAvailCoin(amount, 0);
        coin.m_ID.m_Type = Key::Type::Coinbase;
        walletDB->storeCoin(coin);
    };

    const auto getAllCoins = [&] () -> auto {
        vector<Coin> coins;
        walletDB->visitCoins([&coins](const Coin& c) -> auto
        {
            coins.push_back(c);
            return true;
        });
        return coins;
    };

    const auto getTx = [&](TxID txid) -> auto {
        const auto otx = walletDB->getTx(txid);
        WALLET_CHECK(otx);
        return otx ? otx.get() : TxDescription();
    };

    //
    // Let's start real testing
    //
    LOG_INFO() << "\nTesting assets issue...";
    const auto initialAmount      = beam::Amount(700);
    const auto issueAmount        = beam::Amount(500);
    const auto feeAmount          = beam::Amount(100);
    const auto issueChange        = initialAmount - issueAmount - feeAmount;
    const auto consumeAmount      = Amount(200);
    const auto consumeChange      = issueChange - feeAmount;
    const auto consumeAssetChange = issueAmount - consumeAmount;

    // this will be converted to 400 assets + fee + change
    makeCoin(initialAmount);

    auto coins = walletDB->selectCoins(issueAmount, Zero);
    WALLET_CHECK(coins.size() == 1);
    WALLET_CHECK(coins[0].m_ID.m_Type == Key::Type::Coinbase);
    WALLET_CHECK(coins[0].m_status    == Coin::Available);
    WALLET_CHECK(coins[0].getAmount() == initialAmount);
    WALLET_CHECK(walletDB->getTxHistory().empty());

    helpers::StopWatch sw;
    sw.start();

    const auto assetMeta = std::string("just for test");

    Asset::Full ai;
    ai.m_Metadata.m_Value = toByteBuffer(assetMeta);
    ai.m_Metadata.UpdateHash();
    ai.m_ID = assetId;
    ai.m_LockHeight = 6;
    ai.m_Value = Zero;
    ai.m_Metadata.get_Owner(ai.m_Owner, *sender.m_WalletDB->get_MasterKdf());

    sender.m_WalletDB->saveAsset(ai, 6);
    

    auto issueTxId = sender.m_Wallet.StartTransaction(CreateTransactionParameters(TxType::AssetIssue)
                .SetParameter(TxParameterID::Amount,   issueAmount)
                .SetParameter(TxParameterID::Fee,      feeAmount)
                .SetParameter(TxParameterID::AssetMetadata, assetMeta)
                .SetParameter(TxParameterID::Lifetime, Height(200)));

    waitCount = 1;
    mainReactor->run();

    sw.stop();
    LOG_INFO() << "Issue elapsed time: " << sw.milliseconds() << "ms";

    // check Tx
    const auto issueTx = getTx(issueTxId);
    WALLET_CHECK(issueTx.m_txId        == issueTxId);
    WALLET_CHECK(issueTx.m_amount      == issueAmount);
    WALLET_CHECK(issueTx.m_fee         == feeAmount);
    WALLET_CHECK(issueTx.m_status      == TxStatus::Completed);
    //WALLET_CHECK(issueTx.m_assetOwnerIdx == assetOwnerIdx);
    WALLET_CHECK(issueTx.m_assetId     == assetId);
    WALLET_CHECK(issueTx.m_peerId      == Zero);
    WALLET_CHECK(issueTx.m_myId        == Zero);

    //
    // Here we would have + 2 coins (500 generated asset, 100 change)
    //
    const auto issueCoins = getAllCoins();
    WALLET_CHECK(issueCoins.size() == 3);
    WALLET_CHECK(issueCoins[0].m_ID.m_Type  == Key::Type::Coinbase);
    WALLET_CHECK(issueCoins[0].m_status     == Coin::Spent);
    WALLET_CHECK(issueCoins[0].m_ID.m_Value == initialAmount);
    WALLET_CHECK(issueCoins[0].m_spentTxId  == issueTx.m_txId);
    WALLET_CHECK(issueCoins[1].m_ID.m_Type  == Key::Type::Change);
    WALLET_CHECK(issueCoins[1].m_status     == Coin::Available);
    WALLET_CHECK(issueCoins[1].m_createTxId == issueTx.m_txId);
    WALLET_CHECK(issueCoins[1].getAmount()  == issueChange);
    WALLET_CHECK(issueCoins[2].m_ID.m_Type  == Key::Type::Regular);
    WALLET_CHECK(issueCoins[2].m_status     == Coin::Available);
    WALLET_CHECK(issueCoins[2].m_ID.m_Value == issueAmount);
    WALLET_CHECK(issueCoins[2].m_createTxId == issueTx.m_txId);

    checkTotals(issueChange, issueAmount);
    checkTotals(100, 500);
    LOG_INFO() << "Finished testing assets issue...";

    //
    // Assets consume
    //
    LOG_INFO() << "\nTesting assets consume...";

    sw.start();
    const auto consumeTxId = sender.m_Wallet.StartTransaction(CreateTransactionParameters(TxType::AssetConsume)
                .SetParameter(TxParameterID::Amount,   consumeAmount)
                .SetParameter(TxParameterID::Fee,      feeAmount)
                //.SetParameter(TxParameterID::AssetOwnerIdx, assetOwnerIdx)
                .SetParameter(TxParameterID::Lifetime, Height(200)));

    waitCount = 1;
    mainReactor->run();
    sw.stop();
    LOG_INFO() << "Consume elapsed time: " << sw.milliseconds() << "ms";

    // check Tx
    const auto consumeTx = getTx(consumeTxId);
    WALLET_CHECK(consumeTx.m_txId        == consumeTxId);
    WALLET_CHECK(consumeTx.m_amount      == consumeAmount);
    WALLET_CHECK(consumeTx.m_fee         == feeAmount);
    WALLET_CHECK(consumeTx.m_status      == TxStatus::Completed);
    //WALLET_CHECK(consumeTx.m_assetOwnerIdx == assetOwnerIdx);
    WALLET_CHECK(consumeTx.m_assetId     == assetId);
    WALLET_CHECK(consumeTx.m_peerId      == Zero);
    WALLET_CHECK(consumeTx.m_myId        == Zero);

    //
    // Here we have have + 2 coins, 300 assets change and 200 groth from conversion
    // We would use these in future transaction
    //
    const auto consumeCoins = getAllCoins();
    WALLET_CHECK(consumeCoins.size() == 5);
    WALLET_CHECK(consumeCoins.size() == issueCoins.size() + 2);

    // Beam change coin generated while issuing asset, we spend it during consume
    WALLET_CHECK(consumeCoins[1].m_ID.m_Type  == Key::Type::Change);
    WALLET_CHECK(consumeCoins[1].m_status     == Coin::Spent);
    WALLET_CHECK(consumeCoins[1].m_createTxId == issueTx.m_txId);
    WALLET_CHECK(consumeCoins[1].m_spentTxId  == consumeTx.m_txId);
    WALLET_CHECK(consumeCoins[1].getAmount()  == issueChange);
    // This is our asset, we spend it during consume
    WALLET_CHECK(consumeCoins[2].m_ID.m_Type  == Key::Type::Regular);
    WALLET_CHECK(consumeCoins[2].m_status     == Coin::Consumed);
    WALLET_CHECK(consumeCoins[2].m_ID.m_Value == issueAmount);
    WALLET_CHECK(consumeCoins[2].m_createTxId == issueTx.m_txId);
    WALLET_CHECK(consumeCoins[2].m_spentTxId  == consumeTx.m_txId);
    // This asset change coin, we do not convert all available assets
    WALLET_CHECK(consumeCoins[3].m_ID.m_Type  == Key::Type::Change);
    WALLET_CHECK(consumeCoins[3].m_status     == Coin::Available);
    WALLET_CHECK(consumeCoins[3].m_ID.m_Value == consumeAssetChange);
    WALLET_CHECK(consumeCoins[3].m_createTxId == consumeTx.m_txId);
    // This is beam returned for consumed assets
    WALLET_CHECK(consumeCoins[4].m_ID.m_Type  == Key::Type::Regular);
    WALLET_CHECK(consumeCoins[4].m_status     == Coin::Available);
    WALLET_CHECK(consumeCoins[4].m_ID.m_Value == consumeAmount);
    WALLET_CHECK(consumeCoins[4].m_createTxId == consumeTx.m_txId);

    checkTotals(consumeAmount, consumeAssetChange);
    checkTotals(200, 300);

    LOG_INFO() << "Finished testing assets consume...";

    //
    // Asset txs serialization
    //
    {
        LOG_INFO() << "\nTesting assets tx serialization...";

        auto importDB = createSqliteWalletDB("import_wallet.db", false, false);

        sw.start();
        const auto exported = storage::ExportDataToJson(*walletDB);
        WALLET_CHECK(!exported.empty());
        const auto ires = storage::ImportDataFromJson(*importDB, exported.c_str(), exported.size());
        WALLET_CHECK(ires != false);
        sw.stop();
        LOG_INFO() << "Serialize elapsed time: " << sw.milliseconds() << "ms";

        const auto loadedIssueTx = importDB->getTx(issueTx.m_txId);
        WALLET_CHECK(loadedIssueTx);
        if (loadedIssueTx) {
            WALLET_CHECK(loadedIssueTx->m_assetId     == issueTx.m_assetId);
            //WALLET_CHECK(loadedIssueTx->m_assetOwnerIdx == issueTx.m_assetOwnerIdx);
        }

        const auto loadedConsumeTx = importDB->getTx(consumeTx.m_txId);
        WALLET_CHECK(loadedConsumeTx);
        if (loadedConsumeTx) {
            WALLET_CHECK(loadedConsumeTx->m_assetId     == consumeTx.m_assetId);
            //WALLET_CHECK(loadedConsumeTx->m_assetOwnerIdx == consumeTx.m_assetOwnerIdx);
        }

        LOG_INFO() << "Finished testing assets tx serialization...";
    }

    //
    // Send assets to self test
    //
    LOG_INFO() << "\nTesting send asset to self...";

    const auto selfSendAmount      = consumeAssetChange;
    const auto selfSendAssetChange = consumeAssetChange - selfSendAmount;
    const auto selfSendBeamChange  = consumeAmount - feeAmount;

    sw.start();
    auto selfSendTxId = sender.m_Wallet.StartTransaction(CreateSimpleTransactionParameters()
                            .SetParameter(TxParameterID::MyID,     sender.m_WalletID)
                            .SetParameter(TxParameterID::PeerID,   sender.m_WalletID)
                            .SetParameter(TxParameterID::Amount,   selfSendAmount)
                            .SetParameter(TxParameterID::Fee,      feeAmount)
                            .SetParameter(TxParameterID::Lifetime, Height(200))
                            .SetParameter(TxParameterID::AssetID,  assetId));
    waitCount = 1;
    mainReactor->run();
    sw.stop();
    LOG_INFO() << "Send asset to self elapsed time: " << sw.milliseconds() << "ms";

    // check Tx
    const auto selfSendTx = getTx(selfSendTxId);
    WALLET_CHECK(selfSendTx.m_txId        == selfSendTxId);
    WALLET_CHECK(selfSendTx.m_amount      == selfSendAmount);
    WALLET_CHECK(selfSendTx.m_fee         == feeAmount);
    WALLET_CHECK(selfSendTx.m_status      == TxStatus::Completed);
    WALLET_CHECK(selfSendTx.m_assetId     == assetId);
    WALLET_CHECK(selfSendTx.m_peerId      == sender.m_WalletID);
    WALLET_CHECK(selfSendTx.m_myId        == sender.m_WalletID);

    //
    // Additional 2 coins should be created (100 groth change, 300 incoming asset)
    //
    const auto selfSendCoins = getAllCoins();
    WALLET_CHECK(selfSendCoins.size() == 7);
    WALLET_CHECK(selfSendCoins.size() == consumeCoins.size() + 2);

    // This asset change coin generated before and we've just usded it to send to self
    WALLET_CHECK(selfSendCoins[3].m_ID.m_Type  == Key::Type::Change);
    WALLET_CHECK(selfSendCoins[3].m_status     == Coin::Spent);
    WALLET_CHECK(selfSendCoins[3].m_ID.m_Value == selfSendAmount);
    WALLET_CHECK(selfSendCoins[3].m_createTxId == consumeTx.m_txId);
    WALLET_CHECK(selfSendCoins[3].m_spentTxId  == selfSendTx.m_txId);
    // This is beam returned for consumed assets before, we've used it to pay for self send transaction
    WALLET_CHECK(selfSendCoins[4].m_ID.m_Type  == Key::Type::Regular);
    WALLET_CHECK(selfSendCoins[4].m_status     == Coin::Spent);
    WALLET_CHECK(selfSendCoins[4].m_ID.m_Value == consumeAmount);
    WALLET_CHECK(selfSendCoins[4].m_createTxId == consumeTx.m_txId);
    WALLET_CHECK(selfSendCoins[4].m_spentTxId  == selfSendTx.m_txId);
    // This is our beam change
    WALLET_CHECK(selfSendCoins[5].m_ID.m_Type  == Key::Type::Change);
    WALLET_CHECK(selfSendCoins[5].m_status     == Coin::Available);
    WALLET_CHECK(selfSendCoins[5].m_ID.m_Value == selfSendBeamChange);
    WALLET_CHECK(selfSendCoins[5].m_createTxId == selfSendTx.m_txId);
    // This is our incoming Asset Coin
    WALLET_CHECK(selfSendCoins[6].m_ID.m_Type  == Key::Type::Regular);
    WALLET_CHECK(selfSendCoins[6].m_status     == Coin::Available);
    WALLET_CHECK(selfSendCoins[6].m_ID.m_Value == selfSendAmount);
    WALLET_CHECK(selfSendCoins[6].m_createTxId == selfSendTx.m_txId);

    checkTotals(selfSendBeamChange, selfSendAmount);
    checkTotals(100, 300);
    LOG_INFO() << "Finished testing send asset to self";

    //
    // Try to consume 100 of self-received asset
    //
    LOG_INFO() << "\nTesting self-received asset consume...";

    const auto srConsumeAmount = Amount(200);
    const auto srAssetChange   = selfSendAmount - srConsumeAmount;
    const auto srBeamChange    = 0;

    sw.start();
    const auto srConsumeTxId = sender.m_Wallet.StartTransaction(CreateTransactionParameters(TxType::AssetConsume)
                .SetParameter(TxParameterID::Amount,   srConsumeAmount)
                .SetParameter(TxParameterID::Fee,      feeAmount)
                //.SetParameter(TxParameterID::AssetOwnerIdx, assetOwnerIdx)
                .SetParameter(TxParameterID::Lifetime, Height(200)));

    waitCount = 1;
    mainReactor->run();
    sw.stop();
    LOG_INFO() << "Self-received asset consume elapsed time: " << sw.milliseconds() << "ms";

    // check Tx
    const auto srConsumeTx = getTx(srConsumeTxId);
    WALLET_CHECK(srConsumeTx.m_txId          == srConsumeTxId);
    WALLET_CHECK(srConsumeTx.m_amount        == srConsumeAmount);
    WALLET_CHECK(srConsumeTx.m_fee           == feeAmount);
    WALLET_CHECK(srConsumeTx.m_status        == TxStatus::Completed);
    //WALLET_CHECK(srConsumeTx.m_assetOwnerIdx == assetOwnerIdx);
    WALLET_CHECK(srConsumeTx.m_assetId       == assetId);
    WALLET_CHECK(srConsumeTx.m_peerId        == Zero);
    WALLET_CHECK(srConsumeTx.m_myId          == Zero);

    //
    // Additional 2 coins should be created (100 asset change, 200 groth from conversion)
    //
    const auto srConsumeCoins = getAllCoins();
    WALLET_CHECK(srConsumeCoins.size() == 9);
    WALLET_CHECK(srConsumeCoins.size() == selfSendCoins.size() + 2);

    // This is our beam fee source
    WALLET_CHECK(srConsumeCoins[5].m_ID.m_Type  == Key::Type::Change);
    WALLET_CHECK(srConsumeCoins[5].m_status     == Coin::Spent);
    WALLET_CHECK(srConsumeCoins[5].m_ID.m_Value == selfSendBeamChange);
    WALLET_CHECK(srConsumeCoins[5].m_createTxId == selfSendTx.m_txId);
    WALLET_CHECK(srConsumeCoins[5].m_spentTxId == srConsumeTx.m_txId);
    // This is our asset source
    WALLET_CHECK(srConsumeCoins[6].m_ID.m_Type  == Key::Type::Regular);
    WALLET_CHECK(srConsumeCoins[6].m_status     == Coin::Consumed);
    WALLET_CHECK(srConsumeCoins[6].m_ID.m_Value == selfSendAmount);
    WALLET_CHECK(srConsumeCoins[6].m_createTxId == selfSendTx.m_txId);
    WALLET_CHECK(srConsumeCoins[6].m_spentTxId == srConsumeTx.m_txId);
    // This is our asset change
    WALLET_CHECK(srConsumeCoins[7].m_ID.m_Type  == Key::Type::Change);
    WALLET_CHECK(srConsumeCoins[7].m_status     == Coin::Available);
    WALLET_CHECK(srConsumeCoins[7].m_ID.m_Value == srAssetChange);
    WALLET_CHECK(srConsumeCoins[7].m_createTxId == srConsumeTx.m_txId);
    // This is beam generated from consumed asset
    WALLET_CHECK(srConsumeCoins[8].m_ID.m_Type  == Key::Type::Regular);
    WALLET_CHECK(srConsumeCoins[8].m_status     == Coin::Available);
    WALLET_CHECK(srConsumeCoins[8].m_ID.m_Value == srConsumeAmount);
    WALLET_CHECK(srConsumeCoins[8].m_createTxId == srConsumeTx.m_txId);

    checkTotals(srConsumeAmount, srAssetChange);
    checkTotals(200, 100);

    LOG_INFO() << "Finished testing self-received assets consume...";

    //
    // Send asset to somebody else
    //
    LOG_INFO() << "\nTesting sending assets to third-party...";

    const auto receiverWalletDB = createSqliteWalletDB("receiver_wallet.db", false, true);
    receiverWalletDB->AllocateKidRange(100500);
    TestWalletRig receiver(receiverWalletDB, stopReactor);
    WALLET_CHECK(receiver.m_WalletDB->getTxHistory().empty());
    RegisterAssetCreators(receiver.m_Wallet);

    const auto getRcvTx = [&](TxID txid) -> auto {
        const auto otx = receiverWalletDB->getTx(txid);
        WALLET_CHECK(otx);
        return otx ? otx.get() : TxDescription();
    };

     const auto makeRcvCoin = [&receiverWalletDB](Amount amount) {
        auto coin = CreateAvailCoin(amount, 0);
        coin.m_ID.m_Type = Key::Type::Coinbase;
        receiverWalletDB->storeCoin(coin);
    };

    const auto getAllRcvCoins = [&] () -> auto {
        vector<Coin> coins;
        receiverWalletDB->visitCoins([&coins](const Coin& c) -> auto {
            coins.push_back(c);
            return true;
        });
        return coins;
    };

    sw.start();
    const auto send3rpAmount      = srAssetChange;
    const auto send3rpAssetChange = 0;
    const auto send3rpBeamChange  = srConsumeAmount - feeAmount;

    auto send3rpTxId = sender.m_Wallet.StartTransaction(CreateSimpleTransactionParameters()
        .SetParameter(TxParameterID::MyID,     sender.m_WalletID)
        .SetParameter(TxParameterID::PeerID,   receiver.m_WalletID)
        .SetParameter(TxParameterID::Amount,   srAssetChange)
        .SetParameter(TxParameterID::Fee,      feeAmount)
        .SetParameter(TxParameterID::Lifetime, Height(200))
        .SetParameter(TxParameterID::AssetID,  assetId)
        .SetParameter(TxParameterID::PeerResponseTime, Height(20)));

    waitCount = 2;
    mainReactor->run();
    sw.stop();
    cout << "Sending assets to third-party elapsed time: " << sw.milliseconds() << " ms\n";

    // check sender Tx
    const auto send3rpTx = getTx(send3rpTxId);
    WALLET_CHECK(send3rpTx.m_txId         == send3rpTxId);
    WALLET_CHECK(send3rpTx.m_amount       == send3rpAmount);
    WALLET_CHECK(send3rpTx.m_fee          == feeAmount);
    WALLET_CHECK(send3rpTx.m_status       == TxStatus::Completed);
    WALLET_CHECK(send3rpTx.m_sender       == true);
    WALLET_CHECK(send3rpTx.m_selfTx       == false);
    WALLET_CHECK(send3rpTx.m_assetId      == assetId);
    WALLET_CHECK(send3rpTx.m_peerId       == receiver.m_WalletID);
    WALLET_CHECK(send3rpTx.m_myId         == sender.m_WalletID);

    // check receiver tx
    const auto send3rpRTx = getRcvTx(send3rpTxId);
    WALLET_CHECK(send3rpRTx.m_txId         == send3rpTxId);
    WALLET_CHECK(send3rpRTx.m_amount       == send3rpAmount);
    WALLET_CHECK(send3rpRTx.m_fee          == feeAmount);
    WALLET_CHECK(send3rpRTx.m_status       == TxStatus::Completed);
    WALLET_CHECK(send3rpRTx.m_sender       == false);
    WALLET_CHECK(send3rpRTx.m_selfTx       == false);
    WALLET_CHECK(send3rpRTx.m_assetId      == assetId);
    WALLET_CHECK(send3rpRTx.m_peerId       == sender.m_WalletID);
    WALLET_CHECK(send3rpRTx.m_myId         == receiver.m_WalletID);

    //
    // Additional 1 coins should be created (100 groth change)
    //
    const auto send3rpCoins = getAllCoins();
    WALLET_CHECK(send3rpCoins.size() == 10);
    WALLET_CHECK(send3rpCoins.size() == srConsumeCoins.size() + 1);

    // This is spent asset
    WALLET_CHECK(send3rpCoins[7].m_ID.m_Type  == Key::Type::Change);
    WALLET_CHECK(send3rpCoins[7].m_status     == Coin::Spent);
    WALLET_CHECK(send3rpCoins[7].getAmount()  == send3rpAmount);
    WALLET_CHECK(send3rpCoins[7].m_createTxId == srConsumeTx.m_txId);
    WALLET_CHECK(send3rpCoins[7].m_spentTxId  == send3rpTx.m_txId);
    // This is spent for fee + change
    WALLET_CHECK(send3rpCoins[8].m_ID.m_Type  == Key::Type::Regular);
    WALLET_CHECK(send3rpCoins[8].m_status     == Coin::Spent);
    WALLET_CHECK(send3rpCoins[8].getAmount()  == feeAmount + send3rpBeamChange);
    WALLET_CHECK(send3rpCoins[8].m_createTxId == srConsumeTx.m_txId);
    WALLET_CHECK(send3rpCoins[7].m_spentTxId  == send3rpTx.m_txId);
    // This is beam change
    WALLET_CHECK(send3rpCoins[9].m_ID.m_Type  == Key::Type::Change);
    WALLET_CHECK(send3rpCoins[9].m_status     == Coin::Available);
    WALLET_CHECK(send3rpCoins[9].getAmount()  == send3rpBeamChange);
    WALLET_CHECK(send3rpCoins[9].m_createTxId == send3rpTx.m_txId);

    checkTotals(send3rpBeamChange, 0);
    checkTotals(100, 0);

    //
    // Receiver side, 1 coin of 100 assets should be created
    // We also add 100 groth for future fees
    //
    const auto send3rpRcvCoins = getAllRcvCoins();
    WALLET_CHECK(send3rpRcvCoins.size() == 1);
    WALLET_CHECK(send3rpRcvCoins[0].m_ID.m_Type  == Key::Type::Regular);
    WALLET_CHECK(send3rpRcvCoins[0].m_status     == Coin::Available);
    WALLET_CHECK(send3rpRcvCoins[0].getAmount()  == send3rpTx.m_amount);
    WALLET_CHECK(send3rpRcvCoins[0].m_createTxId == send3rpTx.m_txId);
    LOG_INFO() << "Finished testing sending assets to third-party...";

    //
    // Try to burn assets we don't own, this should fail
    //
    LOG_INFO() << "\nTesting consuming not owned assets...";

    // Add fee coin for receiver
    makeRcvCoin(feeAmount);
    const auto rcvCoins = getAllRcvCoins();
    WALLET_CHECK(rcvCoins.size() == 2);
    WALLET_CHECK(rcvCoins[1].m_ID.m_Type  == Key::Type::Coinbase);
    WALLET_CHECK(rcvCoins[1].m_status     == Coin::Available);
    WALLET_CHECK(rcvCoins[1].getAmount()  == feeAmount);

    const auto nonOwnedConsumeAmount = send3rpAmount;
    const auto nonOwnedAssetChange   = 0;
    const auto nonOwnedBeamChange    = 0;

    sw.start();
    const auto nonOwnedConsumeTxId = receiver.m_Wallet.StartTransaction(CreateTransactionParameters(TxType::AssetConsume)
                .SetParameter(TxParameterID::Amount,         nonOwnedConsumeAmount)
                .SetParameter(TxParameterID::Fee,            feeAmount)
                .SetParameter(TxParameterID::AssetMetadata,  assetMeta)
                .SetParameter(TxParameterID::Lifetime,       Height(200)));

    // Do not run reactor, transaction should be failed with no inputs already
    LOG_INFO() << "Consuming not owned assets elapsed time: " << sw.milliseconds() << "ms";

    // check tx
    const auto nonOwnedConsumeTx = getRcvTx(nonOwnedConsumeTxId);
    WALLET_CHECK(nonOwnedConsumeTx.m_txId          == nonOwnedConsumeTxId);
    WALLET_CHECK(nonOwnedConsumeTx.m_amount        == nonOwnedConsumeAmount);
    WALLET_CHECK(nonOwnedConsumeTx.m_fee           == feeAmount);
    WALLET_CHECK(nonOwnedConsumeTx.m_status        == TxStatus::Failed);
    WALLET_CHECK(nonOwnedConsumeTx.m_failureReason == TxFailureReason::NoInputs);
    WALLET_CHECK(nonOwnedConsumeTx.m_assetMeta     == assetMeta);
    WALLET_CHECK(nonOwnedConsumeTx.m_assetId       != assetId);

    LOG_INFO() << "Finished Testing consuming not owned assets...";

    //
    // Send assets back
    //
    LOG_INFO() << "\nTesting returning assets from third-party...";

    const auto recv3rpAmount = send3rpAmount;
    const auto recv3rpBeamChange = Amount(0);
    const auto recv3rpAssetChange = send3rpAmount - recv3rpAmount;

    sw.start();
    auto recv3rpTxId = receiver.m_Wallet.StartTransaction(CreateSimpleTransactionParameters()
        .SetParameter(TxParameterID::MyID,     receiver.m_WalletID)
        .SetParameter(TxParameterID::PeerID,   sender.m_WalletID)
        .SetParameter(TxParameterID::Amount,   recv3rpAmount)
        .SetParameter(TxParameterID::Fee,      feeAmount)
        .SetParameter(TxParameterID::Lifetime, Height(200))
        .SetParameter(TxParameterID::AssetID,  assetId)
        .SetParameter(TxParameterID::PeerResponseTime, Height(20)));

    waitCount = 2;
    mainReactor->run();
    sw.stop();
    cout << "Returning assets from  elapsed time: " << sw.milliseconds() << " ms\n";

    // check sender Tx
    const auto recv3rpTx = getTx(recv3rpTxId);
    WALLET_CHECK(recv3rpTx.m_txId         == recv3rpTxId);
    WALLET_CHECK(recv3rpTx.m_amount       == recv3rpAmount);
    WALLET_CHECK(recv3rpTx.m_fee          == feeAmount);
    WALLET_CHECK(recv3rpTx.m_status       == TxStatus::Completed);
    WALLET_CHECK(recv3rpTx.m_sender       == false);
    WALLET_CHECK(recv3rpTx.m_selfTx       == false);
    WALLET_CHECK(recv3rpTx.m_assetId      == assetId);
    WALLET_CHECK(recv3rpTx.m_peerId       == receiver.m_WalletID);
    WALLET_CHECK(recv3rpTx.m_myId         == sender.m_WalletID);

    const auto recv3rpRTx = getRcvTx(recv3rpTxId);
    WALLET_CHECK(recv3rpRTx.m_txId         == recv3rpTxId);
    WALLET_CHECK(recv3rpRTx.m_amount       == recv3rpAmount);
    WALLET_CHECK(recv3rpRTx.m_fee          == feeAmount);
    WALLET_CHECK(recv3rpRTx.m_status       == TxStatus::Completed);
    WALLET_CHECK(recv3rpRTx.m_sender       == true);
    WALLET_CHECK(recv3rpRTx.m_selfTx       == false);
    WALLET_CHECK(recv3rpRTx.m_assetId      == assetId);
    WALLET_CHECK(recv3rpRTx.m_peerId       == sender.m_WalletID);
    WALLET_CHECK(recv3rpRTx.m_myId         == receiver.m_WalletID);

    // check 3rdparty coins, all should be spent, no new coins
    const auto recv3rpRCoins = getAllRcvCoins();
    WALLET_CHECK(recv3rpRCoins.size() == rcvCoins.size());
    WALLET_CHECK(recv3rpRCoins.size() == 2);
    WALLET_CHECK(recv3rpRCoins[0].m_ID.m_Type == Key::Type::Regular);
    WALLET_CHECK(recv3rpRCoins[0].m_status    == Coin::Spent);
    WALLET_CHECK(recv3rpRCoins[1].m_ID.m_Type == Key::Type::Coinbase);
    WALLET_CHECK(recv3rpRCoins[1].m_status    == Coin::Spent);

    //
    // Check coins, +1 coin should be created (100 assets)
    // We should have 100 groth & 100 assets
    //
    const auto recv3rpCoins = getAllCoins();
    WALLET_CHECK(recv3rpCoins.size() == send3rpCoins.size() +  1);
    WALLET_CHECK(recv3rpCoins.size() == 11);
    // This is our incoming asseet
    WALLET_CHECK(recv3rpCoins[10].m_ID.m_Type  == Key::Type::Regular);
    WALLET_CHECK(recv3rpCoins[10].m_status     == Coin::Available);
    WALLET_CHECK(recv3rpCoins[10].getAmount()  == recv3rpAmount);
    WALLET_CHECK(recv3rpCoins[10].m_createTxId == recv3rpRTx.m_txId);

    checkTotals(send3rpBeamChange, recv3rpAmount);
    checkTotals(100, 100);
    LOG_INFO() << "Finished testing returning assets from third-party...";

    //
    // Consume received assets
    //
    LOG_INFO() << "\nTesting received assets consume...";

    const auto recvConsumeAmount = recv3rpAmount;
    const auto recvAssetChange   = 0;
    const auto recvBeamChange    = 0;

    sw.start();
    const auto recvConsumeTxId = sender.m_Wallet.StartTransaction(CreateTransactionParameters(TxType::AssetConsume)
                .SetParameter(TxParameterID::Amount,   recvConsumeAmount)
                .SetParameter(TxParameterID::Fee,      feeAmount)
                //.SetParameter(TxParameterID::AssetOwnerIdx, assetOwnerIdx)
                .SetParameter(TxParameterID::Lifetime, Height(200)));

    waitCount = 1;
    mainReactor->run();
    sw.stop();
    LOG_INFO() << "Received assets consume elapsed time: " << sw.milliseconds() << "ms";

    // check Tx
    const auto recvConsumeTx = getTx(recvConsumeTxId);
    WALLET_CHECK(recvConsumeTx.m_txId          == recvConsumeTxId);
    WALLET_CHECK(recvConsumeTx.m_amount        == recvConsumeAmount);
    WALLET_CHECK(recvConsumeTx.m_fee           == feeAmount);
    WALLET_CHECK(recvConsumeTx.m_status        == TxStatus::Completed);
    //WALLET_CHECK(recvConsumeTx.m_assetOwnerIdx == assetOwnerIdx);
    WALLET_CHECK(recvConsumeTx.m_assetId       == assetId);
    WALLET_CHECK(recvConsumeTx.m_peerId        == Zero);
    WALLET_CHECK(recvConsumeTx.m_myId          == Zero);

    //
    // Check coins
    // +1 additional coins (100 groth from conversion)
    //
    const auto recvConsumeCoins = getAllCoins();
    WALLET_CHECK(recvConsumeCoins.size() == recv3rpCoins.size() + 1);
    WALLET_CHECK(recvConsumeCoins.size() == 12);
    // Spent on fee
    WALLET_CHECK(recvConsumeCoins[9].m_ID.m_Type  == Key::Type::Change);
    WALLET_CHECK(recvConsumeCoins[9].m_status     == Coin::Spent);
    WALLET_CHECK(recvConsumeCoins[9].getAmount()  == feeAmount);
    WALLET_CHECK(recvConsumeCoins[9].m_createTxId == send3rpTx.m_txId);
    WALLET_CHECK(recvConsumeCoins[9].m_spentTxId  == recvConsumeTx.m_txId);
    // Spent asset
    WALLET_CHECK(recvConsumeCoins[10].m_ID.m_Type  == Key::Type::Regular);
    WALLET_CHECK(recvConsumeCoins[10].m_status     == Coin::Consumed);
    WALLET_CHECK(recvConsumeCoins[10].getAmount()  == recvConsumeAmount);
    WALLET_CHECK(recvConsumeCoins[10].m_createTxId == recv3rpRTx.m_txId);
    WALLET_CHECK(recvConsumeCoins[10].m_spentTxId  == recvConsumeTx.m_txId);
    // Converted from assets
    WALLET_CHECK(recvConsumeCoins[11].m_ID.m_Type  == Key::Type::Regular);
    WALLET_CHECK(recvConsumeCoins[11].m_status     == Coin::Available);
    WALLET_CHECK(recvConsumeCoins[11].getAmount()  == recvConsumeAmount);
    WALLET_CHECK(recvConsumeCoins[11].m_createTxId == recvConsumeTx.m_txId);

    checkTotals(recvConsumeAmount, 0);
    checkTotals(100, 0);

    LOG_INFO() << "\nFinsihed testing received assets consume...";

    // Huh, at last we're done
    LOG_INFO() << "Finished testing assets...";
}

int main () {
    const auto logLevel = LOG_LEVEL_DEBUG;
    const auto logger = beam::Logger::create(logLevel, logLevel);
    LOG_INFO() << "Assets test - starting";

    auto& rules = beam::Rules::get();
    rules.CA.Enabled = true;
    rules.FakePoW    = true;
    rules.pForks[1].m_Height  = 10;
    rules.pForks[2].m_Height  = 20;
    rules.UpdateChecksum();

    // TestAssets(); Disabled while we modify the assets logic

    LOG_INFO() << "Assets test - completed";
    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}
