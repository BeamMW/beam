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
#include "wallet/core/common.h"
#include "utility/logger.h"
#include "wallet/core/wallet_network.h"
#include "wallet/core/base_transaction.h"
#include "wallet/core/simple_transaction.h"
#include "wallet/transactions/assets/assets_reg_creators.h"
#include "node/node.h"
#include "core/unittest/mini_blockchain.h"
#include "utility/test_helpers.h"
#include "test_helpers.h"
WALLET_TEST_INIT
#include "wallet_test_environment.cpp"

void InitTestNode(Node& node, const ByteBuffer& binaryTreasury, Node::IObserver* observer,
                  Key::IPKdf::Ptr ownerKey, uint16_t port = 32125, uint32_t powSolveTime = 200,
                  const std::string& path = "mytest.db", const std::vector<io::Address>& peers = {},
                  bool miningNode = true)
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

void TestAssets() {
    //
    // Assets issue
    //
    LOG_INFO() << "\nPreparing for assets test...";

    beam::io::Reactor::Ptr reactor{beam::io::Reactor::create()};
    beam::io::Reactor::Scope scope(*reactor);

    int waitCount = 0;
    const auto stopReactor = [&waitCount, reactor](auto)
    {
        --waitCount;
        if (waitCount == 0)
        {
            reactor->stop();
        }
    };

    const auto  receiverDB = createSqliteWalletDB("receiver_wallet.db", false, true);
    const AmountList kDefaultTestAmounts = {50000000000, 50000000000, 50000000000, 50000000000, 50000000000, 50000000000};
    const auto receiverTreasury = createTreasury(receiverDB, kDefaultTestAmounts);

    Node node;
    Height waitBlock = 0;
    NodeObserver observer([&](){
        const auto cursor = node.get_Processor().m_Cursor;
        if (cursor.m_Sid.m_Height == Rules::get().pForks[1].m_Height) {
            LOG_INFO () << "Reached fork 1...";
        }
        if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height) {
            LOG_INFO () << "Reached fork 2...";
            reactor->stop();
            return;
        }
        if (waitBlock && cursor.m_Sid.m_Height == waitBlock) {
            LOG_INFO () << "Reached block " << waitBlock << "...";
            reactor->stop();
            return;
        }
        if (cursor.m_Sid.m_Height >= 100) {
            LOG_INFO () << "Reached max allowed block...";
            WALLET_CHECK(!"Test should complete before block 100. Something went wrong.");
            reactor->stop();
            return;
        }
    });

    InitTestNode(node, receiverTreasury, &observer, receiverDB->get_MasterKdf());
    TestWalletRig receiver(receiverDB, stopReactor, TestWalletRig::RegularWithoutPoWBbs);

    auto ownerDB = createSqliteWalletDB("owner_wallet.db", false, true);
    TestWalletRig owner(ownerDB, stopReactor, TestWalletRig::RegularWithoutPoWBbs);

    LOG_INFO() << "\nStarting node and waiting until fork2...";
    reactor->run();

    //
    // Here fork2 should be reached
    //
    auto cursor = node.get_Processor().m_Cursor;
    WALLET_CHECK(receiverDB->getTxHistory().empty());
    WALLET_CHECK(cursor.m_Sid.m_Height > Rules::get().pForks[1].m_Height);
    WALLET_CHECK(cursor.m_Sid.m_Height >= Rules::get().pForks[2].m_Height);

    //
    // And enough BEAM mined
    //
    storage::Totals totals(*receiverDB);
    const auto mined = AmountBig::get_Lo(totals.GetBeamTotals().Avail);
    LOG_INFO() << "Beam mined " << PrintableAmount(mined);

    const auto deposit = Rules::get().CA.DepositForList;
    const auto fee = Amount(100);
    const auto initial = Rules::get().CA.DepositForList * 2 + fee * 40; // 40 should be enough;

    LOG_INFO() << "Beam necessary for test " << PrintableAmount(initial);
    WALLET_CHECK(initial <= mined);

    //
    // Owner wallet should be empty and ready for testing
    //
    const std::string ASSET1_META  = "SOME STRING 1";
    const std::string ASSET2_META  = "SOME STRING 2";
    const std::string NOASSET_META = "THIS ASSET DOESN'T EXIST";
    Asset::ID ASSET1_ID = 1;
    Asset::ID ASSET2_ID = 2;
    Asset::ID NOASSET_ID = 3;

    const auto checkOwnerTotals = [&] (Amount beam, Amount asset1, Amount asset2) {
        storage::Totals allTotals(*ownerDB);

        auto availBM = AmountBig::get_Lo(allTotals.GetBeamTotals().Avail);
        auto availA1 = AmountBig::get_Lo(allTotals.GetTotals(ASSET1_ID).Avail);
        auto availA2 = AmountBig::get_Lo(allTotals.GetTotals(ASSET2_ID).Avail);

        WALLET_CHECK( availBM == beam);
        WALLET_CHECK(availA1 == asset1);
        WALLET_CHECK(availA2 == asset2);
    };

    checkOwnerTotals(0, 0, 0);
    beam::wallet::RegisterAssetCreators(*receiver.m_Wallet);
    beam::wallet::RegisterAssetCreators(*owner.m_Wallet);

    const auto getTx = [&](const IWalletDB::Ptr& db, TxID txid) -> auto {
      const auto otx = db->getTx(txid);
      WALLET_CHECK(otx.is_initialized());
      return otx.is_initialized() ? *otx : TxDescription();
    };

    TxDescription tx;
    auto runTest = [&](const char* name, const std::function<TxID ()>& test, int wcnt = 1, bool owner = true) {
        LOG_INFO() << "\nTesting " << name << "...";

        helpers::StopWatch sw;
        sw.start();
        waitCount = wcnt;
        const auto txid = test();
        reactor->run();
        sw.stop();
        LOG_INFO() << name << ", elapsed time: " << sw.milliseconds() << "ms";

        auto db = owner ? ownerDB : receiverDB;
        tx = getTx(db, txid);
        WALLET_CHECK(tx.m_txId == txid);
    };

    //
    // Assets flag not set, fail any asset tx
    //
    runTest("assets flag is false", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetReg)
            .SetParameter(TxParameterID::Amount, beam::Amount(0))
            .SetParameter(TxParameterID::Fee, beam::Amount(100))
            .SetParameter(TxParameterID::AssetMetadata, ASSET1_META));
    });
    WALLET_CHECK(tx.m_status        == TxStatus::Failed);
    WALLET_CHECK(tx.m_failureReason == TxFailureReason::AssetsDisabledInWallet);
    g_AssetsEnabled = true;

    //
    // ASSET REGISTER
    //

    // not enough beam
    runTest("register, not enough BEAM", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetReg)
            .SetParameter(TxParameterID::Amount, Rules::get().CA.DepositForList)
            .SetParameter(TxParameterID::Fee, beam::Amount(100))
            .SetParameter(TxParameterID::AssetMetadata, ASSET1_META));
    });
    WALLET_CHECK(tx.m_status        == TxStatus::Failed);
    WALLET_CHECK(tx.m_failureReason == TxFailureReason::NoInputs);

    // send some beam to the owner's wallet
    runTest("send BEAM to owner", [&] {
       return receiver.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
            .SetParameter(TxParameterID::Amount, initial)
            .SetParameter(TxParameterID::Fee, beam::Amount(100))
            .SetParameter(TxParameterID::MyID, receiver.m_WalletID)
            .SetParameter(TxParameterID::PeerID, owner.m_WalletID));
    }, 2, false);
    LOG_INFO() << "Now owner has " << PrintableAmount(storage::Totals(*ownerDB).GetBeamTotals().Avail);

    // amount too small
    runTest("register, amount is too small", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetReg)
            .SetParameter(TxParameterID::Amount, beam::Amount(0))
            .SetParameter(TxParameterID::Fee, beam::Amount(100))
            .SetParameter(TxParameterID::AssetMetadata, ASSET1_META));
    });
    WALLET_CHECK(tx.m_status        == TxStatus::Failed);
    WALLET_CHECK(tx.m_failureReason == TxFailureReason::RegisterAmountTooSmall);

    // fee too small
    // TODO: Uncomment when we'll create base builder that checks fees. Now it is assumed to be checked by the CLI
    //
    //runTest("register, fee is too small", [&] {
    //    return sender.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetReg)
    //        .SetParameter(TxParameterID::Amount, beam::Amount(Rules::get().CA.DepositForList))
    //        .SetParameter(TxParameterID::Fee, beam::Amount(0))
    //        .SetParameter(TxParameterID::AssetMetadata, ASSET1_META));
    // });
    //WALLET_CHECK(tx.m_status        == TxStatus::Failed);
    //WALLET_CHECK(tx.m_failureReason == TxFailureReason::FeeIsTooSmall);

    // missing meta
    runTest("register, missing meta", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetReg)
            .SetParameter(TxParameterID::Amount, beam::Amount(Rules::get().CA.DepositForList))
            .SetParameter(TxParameterID::Fee, beam::Amount(100)));
    });
    WALLET_CHECK(tx.m_status        == TxStatus::Failed);
    WALLET_CHECK(tx.m_failureReason == TxFailureReason::FailedToGetParameter);

    // empty meta
    runTest("register, empty meta", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetReg)
            .SetParameter(TxParameterID::Amount, beam::Amount(Rules::get().CA.DepositForList))
            .SetParameter(TxParameterID::Fee, beam::Amount(100))
            .SetParameter(TxParameterID::AssetMetadata, ""));
    });
    WALLET_CHECK(tx.m_status        == TxStatus::Failed);
    WALLET_CHECK(tx.m_failureReason == TxFailureReason::NoAssetMeta);

    // successfully register asset #1
    auto currBM = initial;
    auto currA1 = Amount(0);
    auto currA2 = Amount(0);
    checkOwnerTotals(currBM, currA1, currA2);

    runTest("register asset #1", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetReg)
            .SetParameter(TxParameterID::Amount, deposit)
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetMetadata, ASSET1_META));
    });

    currBM -= deposit + fee;
    WALLET_CHECK(tx.m_status      == TxStatus::Completed);
    WALLET_CHECK(tx.m_amount      == deposit);
    WALLET_CHECK(tx.m_fee         == fee);
    WALLET_CHECK(tx.m_assetId     == ASSET1_ID);
    WALLET_CHECK(tx.m_peerId      == Zero);
    WALLET_CHECK(tx.m_myId        == Zero);
    WALLET_CHECK(tx.m_assetMeta   == ASSET1_META);
    checkOwnerTotals(currBM, currA1, currA2);

    // second time register the same asset
    runTest("register asset #1 second time", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetReg)
            .SetParameter(TxParameterID::Amount, deposit)
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetMetadata, ASSET1_META));
    });

    WALLET_CHECK(tx.m_status        == TxStatus::Failed);
    WALLET_CHECK(tx.m_failureReason == TxFailureReason::AssetExists);
    WALLET_CHECK(tx.m_assetId       == Asset::s_InvalidID);
    checkOwnerTotals(currBM, currA1, currA2);

    // successfully register asset #2
    runTest("register asset #2", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetReg)
            .SetParameter(TxParameterID::Amount, deposit)
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetMetadata, ASSET2_META));
    });

    currBM -= deposit + fee;
    checkOwnerTotals(currBM, currA1, currA2);
    WALLET_CHECK(tx.m_status      == TxStatus::Completed);
    WALLET_CHECK(tx.m_amount      == deposit);
    WALLET_CHECK(tx.m_fee         == fee);
    WALLET_CHECK(tx.m_assetId     == ASSET2_ID);
    WALLET_CHECK(tx.m_peerId      == Zero);
    WALLET_CHECK(tx.m_myId        == Zero);
    WALLET_CHECK(tx.m_assetMeta   == ASSET2_META);

    // confirm asset #1 by ID
    runTest("confirm asset #1 by ID", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetInfo)
            .SetParameter(TxParameterID::Amount, deposit)
            .SetParameter(TxParameterID::AssetID, ASSET1_ID));
    });

    auto checkConfirmTx = [&](Asset::ID id, const std::string& meta) { ;
        WALLET_CHECK(tx.m_status == TxStatus::Completed);
        WALLET_CHECK(tx.m_assetId == id);
        WALLET_CHECK(tx.m_assetMeta == meta);
        Asset::Full info;
        WALLET_CHECK(tx.GetParameter(TxParameterID::AssetInfoFull, info));
        WALLET_CHECK(info.m_ID == id);
        WALLET_CHECK(info.m_Metadata.m_Value == toByteBuffer(meta));
        Height height = 0;
        WALLET_CHECK(tx.GetParameter(TxParameterID::AssetUnconfirmedHeight, height) && height == 0);
        WALLET_CHECK(tx.GetParameter(TxParameterID::AssetConfirmedHeight, height) && height != 0);
    };
    checkConfirmTx(ASSET1_ID, ASSET1_META);

    // confirm asset #1 by meta
    runTest("confirm asset #1 by meta", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetInfo)
            .SetParameter(TxParameterID::Amount, deposit)
            .SetParameter(TxParameterID::AssetMetadata, ASSET1_META));
    });
    checkConfirmTx(ASSET1_ID, ASSET1_META);

    // confirm asset #2 by ID
    runTest("confirm asset #2 by ID", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetInfo)
            .SetParameter(TxParameterID::Amount, deposit)
            .SetParameter(TxParameterID::AssetID, ASSET2_ID));
    });
    checkConfirmTx(ASSET2_ID, ASSET2_META);

    // confirm asset #2 by meta
    runTest("confirm asset #2 by meta", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetInfo)
            .SetParameter(TxParameterID::Amount, deposit)
            .SetParameter(TxParameterID::AssetMetadata, ASSET2_META));
    });
    checkConfirmTx(ASSET2_ID, ASSET2_META);

    // confirm asset #1 by id, non-owner
    runTest("confirm asset #1 by id, non-owner", [&] {
        return receiver.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetInfo)
            .SetParameter(TxParameterID::Amount, deposit)
            .SetParameter(TxParameterID::AssetID, ASSET1_ID));
    }, 1, false);
    checkConfirmTx(ASSET1_ID, ASSET1_META);

    // confirm asset #1 by meta, non-owner, should FAIL
    runTest("confirm asset #1 by meta, non-owner", [&] {
        return receiver.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetInfo)
            .SetParameter(TxParameterID::Amount, deposit)
            .SetParameter(TxParameterID::AssetMetadata, ASSET1_META));
    }, 1, false);

    WALLET_CHECK(tx.m_status == TxStatus::Failed);
    WALLET_CHECK(tx.m_assetId == Asset::s_InvalidID);
    WALLET_CHECK(tx.m_assetMeta == ASSET1_META);
    Height height = 0;
    WALLET_CHECK(tx.GetParameter(TxParameterID::AssetConfirmedHeight, height) && height == 0);
    WALLET_CHECK(tx.GetParameter(TxParameterID::AssetUnconfirmedHeight, height) && height != 0);

    // confirm asset that does not exist by ID
    runTest("confirm asset that does not exist by ID", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetInfo)
            .SetParameter(TxParameterID::Amount, deposit)
            .SetParameter(TxParameterID::AssetID, NOASSET_ID));
    });
    WALLET_CHECK(tx.m_status == TxStatus::Failed);
    WALLET_CHECK(tx.m_assetId == NOASSET_ID);
    WALLET_CHECK(tx.m_assetMeta.empty());
    height = 0;
    WALLET_CHECK(tx.GetParameter(TxParameterID::AssetConfirmedHeight, height) && height == 0);
    WALLET_CHECK(tx.GetParameter(TxParameterID::AssetUnconfirmedHeight, height) && height != 0);

    // issue asset #1
    auto amount = Amount(200);
    currA1 += amount;
    currBM -= fee;

    runTest("issue asset #1", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetIssue)
            .SetParameter(TxParameterID::Amount, amount)
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetMetadata, ASSET1_META));
    });

    WALLET_CHECK(tx.m_status == TxStatus::Completed);
    WALLET_CHECK(tx.m_assetId == ASSET1_ID);
    WALLET_CHECK(tx.m_assetMeta == ASSET1_META);
    WALLET_CHECK(tx.m_selfTx);
    checkOwnerTotals(currBM, currA1, currA2);

    // consume asset #1
    amount = Amount(100);
    currA1 -= amount;
    currBM -= fee;

    runTest("consume asset #1", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetConsume)
            .SetParameter(TxParameterID::Amount, amount)
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetMetadata, ASSET1_META));
    });

    WALLET_CHECK(tx.m_status == TxStatus::Completed);
    WALLET_CHECK(tx.m_assetId == ASSET1_ID);
    WALLET_CHECK(tx.m_assetMeta == ASSET1_META);
    WALLET_CHECK(tx.m_selfTx);
    checkOwnerTotals(currBM, currA1, currA2);

    // issue asset #2
    amount = Amount(100);
    currA2 += amount;
    currBM -= fee;

    runTest("issue asset #2", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetIssue)
            .SetParameter(TxParameterID::Amount, amount)
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetMetadata, ASSET2_META));
    });

    WALLET_CHECK(tx.m_status == TxStatus::Completed);
    WALLET_CHECK(tx.m_assetId == ASSET2_ID);
    WALLET_CHECK(tx.m_assetMeta == ASSET2_META);
    WALLET_CHECK(tx.m_selfTx);
    checkOwnerTotals(currBM, currA1, currA2);

    // send locked asset, tx should fail on SENDER size, thus only 1 transaction wait
    runTest("send locked asset", [&] {
        return owner.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
            .SetParameter(TxParameterID::Amount, currA1)
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetID, ASSET1_ID)
            .SetParameter(TxParameterID::MyID, owner.m_WalletID)
            .SetParameter(TxParameterID::PeerID, receiver.m_WalletID));
    }, 1);

    WALLET_CHECK(tx.m_status == TxStatus::Failed);
    WALLET_CHECK(tx.m_failureReason == TxFailureReason::AssetLocked);
    WALLET_CHECK(tx.m_assetId == ASSET1_ID);

    // confirm asset #2 by meta
    runTest("confirm asset #2 by meta again", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetInfo)
            .SetParameter(TxParameterID::Amount, deposit)
            .SetParameter(TxParameterID::AssetMetadata, ASSET2_META));
    });
    checkConfirmTx(ASSET2_ID, ASSET2_META);

    // wait until asset2 becomes unlocked (asset1 becomes unlocked earlier)
    auto asset2 = ownerDB->findAsset(ASSET2_ID);
    WALLET_CHECK(asset2.is_initialized());
    waitBlock = asset2->m_LockHeight + Rules::get().CA.LockPeriod + 1;
    reactor->run();

    // send half of asset #1
    auto totalA1 = currA1;
    amount = currA1 / 2;
    currA1 -= amount;
    currBM -= fee;

    runTest("send half of asset #1", [&] {
        return owner.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
            .SetParameter(TxParameterID::Amount, amount)
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetID, ASSET1_ID)
            .SetParameter(TxParameterID::MyID, owner.m_WalletID)
            .SetParameter(TxParameterID::PeerID, receiver.m_WalletID));
    }, 2);

    WALLET_CHECK(tx.m_status == TxStatus::Completed);
    WALLET_CHECK(tx.m_assetId == ASSET1_ID);
    checkOwnerTotals(currBM, currA1, currA2);

    // send the rest of asset #1
    amount  = currA1;
    currA1 -= amount;
    currBM -= fee;

    runTest("send the rest of asset #1", [&] {
        return owner.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
            .SetParameter(TxParameterID::Amount, amount)
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetID, ASSET1_ID)
            .SetParameter(TxParameterID::MyID, owner.m_WalletID)
            .SetParameter(TxParameterID::PeerID, receiver.m_WalletID));
    }, 2);

    WALLET_CHECK(tx.m_status == TxStatus::Completed);
    WALLET_CHECK(tx.m_assetId == ASSET1_ID);
    WALLET_CHECK(currA1 == 0);
    checkOwnerTotals(currBM, currA1, currA2);

    // consume non-owned asset
    runTest("consume non-owned asset", [&] {
        return receiver.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetConsume)
            .SetParameter(TxParameterID::Amount, totalA1)
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetMetadata, ASSET1_META));
    }, 1, false);

    WALLET_CHECK(tx.m_status == TxStatus::Failed);
    WALLET_CHECK(tx.m_assetMeta == ASSET1_META);
    WALLET_CHECK(tx.m_failureReason == TxFailureReason::AssetConfirmFailed);

    // send asset #1 back
    amount = totalA1;
    currA1 = amount;

    runTest("send asset #1 back", [&] {
        return receiver.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
            .SetParameter(TxParameterID::Amount, amount)
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetID, ASSET1_ID)
            .SetParameter(TxParameterID::MyID, receiver.m_WalletID)
            .SetParameter(TxParameterID::PeerID, owner.m_WalletID));
    }, 2, false);

    WALLET_CHECK(tx.m_status == TxStatus::Completed);
    WALLET_CHECK(tx.m_assetId == ASSET1_ID);
    checkOwnerTotals(currBM, currA1, currA2);

    // unregister used asset
    runTest("unregister used asset", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetUnreg)
            .SetParameter(TxParameterID::Amount, deposit)
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetMetadata, ASSET1_META));
    });
    WALLET_CHECK(tx.m_status == TxStatus::Failed);
    WALLET_CHECK(tx.m_failureReason == TxFailureReason::AssetInUse);

    // consume asset #1
    amount  = currA1;
    currA1 -= amount;
    currBM -= fee;

    runTest("consume asset #1", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetConsume)
            .SetParameter(TxParameterID::Amount, amount)
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetMetadata, ASSET1_META));
    });

    WALLET_CHECK(tx.m_status == TxStatus::Completed);
    WALLET_CHECK(tx.m_assetMeta == ASSET1_META);
    checkOwnerTotals(currBM, 0, currA2);

    // consume asset #2
    amount  = currA2;
    currA2 -= amount;
    currBM -= fee;

    runTest("consume asset #2", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetConsume)
            .SetParameter(TxParameterID::Amount, amount)
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetMetadata, ASSET2_META));
    });

    WALLET_CHECK(tx.m_status == TxStatus::Completed);
    WALLET_CHECK(tx.m_assetMeta == ASSET2_META);
    checkOwnerTotals(currBM, 0, 0);

    // consume excess amount
    runTest("consume excess amount", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetConsume)
            .SetParameter(TxParameterID::Amount, 100)
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetMetadata, ASSET1_META));
    });

    WALLET_CHECK(tx.m_status == TxStatus::Failed);
    WALLET_CHECK(tx.m_failureReason == TxFailureReason::NoInputs);
    WALLET_CHECK(tx.m_assetMeta == ASSET1_META);
    checkOwnerTotals(currBM, 0, 0);

    // consume invalid asset
    runTest("consume invalid asset", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetConsume)
            .SetParameter(TxParameterID::Amount, amount)
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetMetadata, NOASSET_META));
    });

    WALLET_CHECK(tx.m_status == TxStatus::Failed);
    WALLET_CHECK(tx.m_failureReason == TxFailureReason::AssetConfirmFailed);
    WALLET_CHECK(tx.m_assetMeta == NOASSET_META);
    checkOwnerTotals(currBM, 0, 0);

    // unregister locked asset
    runTest("unregister locked asset", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetUnreg)
            .SetParameter(TxParameterID::Amount, deposit)
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetMetadata, ASSET2_META));
    });
    WALLET_CHECK(tx.m_status == TxStatus::Failed);
    WALLET_CHECK(tx.m_failureReason == TxFailureReason::AssetLocked);

    // wait until asset2 becomes unlocked (asset1 becomes unlocked earlier)
    asset2 = ownerDB->findAsset(ASSET2_ID);
    WALLET_CHECK(asset2.is_initialized());
    waitBlock = asset2->m_LockHeight + Rules::get().CA.LockPeriod + 1;
    reactor->run();

    // unregister asset #1
    amount = deposit;
    currBM += deposit - fee;
    runTest("unregister asset #1", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetUnreg)
            .SetParameter(TxParameterID::Amount, deposit)
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetMetadata, ASSET1_META));
    });

    WALLET_CHECK(tx.m_status == TxStatus::Completed);
    checkOwnerTotals(currBM, 0, 0);

    // unregister asset #2
    amount = deposit;
    currBM += deposit - fee;
    runTest("unregister asset #2", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetUnreg)
            .SetParameter(TxParameterID::Amount, deposit)
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetMetadata, ASSET2_META));
    });

    WALLET_CHECK(tx.m_status == TxStatus::Completed);
    checkOwnerTotals(currBM, 0, 0);

    //
    // At last we're done!
    //
    LOG_INFO() << "Finished testing assets...";
}

int main () {
    const auto logLevel = LOG_LEVEL_DEBUG;
    const auto logger = beam::Logger::create(logLevel, logLevel);
    LOG_INFO() << "Assets test - starting";

    auto& rules = beam::Rules::get();
    WALLET_CHECK(rules.CA.LockPeriod == rules.MaxRollback);

    rules.CA.Enabled          = true;
    rules.CA.LockPeriod       = 20;
    rules.CA.DepositForList   = rules.Coin * 1000;
    rules.MaxRollback         = 20;
    rules.FakePoW             = true;
    rules.pForks[1].m_Height  = 5;
    rules.pForks[2].m_Height  = 10;
    rules.pForks[3].m_Height  = 15;
    rules.UpdateChecksum();

    TestAssets();

    LOG_INFO() << "Assets test - completed";
    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}
