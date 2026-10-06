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
#include "wallet/core/assets_utils.h"
#include "utility/logger.h"
#include "wallet/core/wallet_network.h"
#include "wallet/core/base_transaction.h"
#include "wallet/core/simple_transaction.h"
#include "wallet/transactions/assets/assets_reg_creators.h"
#include "node/node.h"
#include "core/unittest/mini_blockchain.h"
#include "utility/test_helpers.h"
#include "test_helpers.h"
#include "wallet_test_node.h"
WALLET_TEST_INIT
#include "wallet_test_environment.cpp"

void InitTestNode(Node& node, Rules& r, const ByteBuffer& binaryTreasury, Node::IObserver* observer,
                  Key::IPKdf::Ptr ownerKey, uint16_t port = 32125, uint32_t powSolveTime = 200,
                  const std::string& path = "mytest.db", const std::vector<io::Address>& peers = {},
                  bool miningNode = true)
{
    node.m_Keys.m_pOwner = ownerKey;
    node.m_Cfg.m_Treasury = binaryTreasury;
    ECC::Hash::Processor() << Blob(node.m_Cfg.m_Treasury) >> r.TreasuryChecksum;

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
    r.m_Consensus = Rules::Consensus::FakePoW;

    node.m_Cfg.m_Observer = observer;
    r.UpdateChecksum();
    node.Initialize();
    node.m_PostStartSynced = true;
}

void TestAssets(Rules& r) {
    //
    // Assets issue
    //
    BEAM_LOG_INFO() << "\nPreparing for assets test...";

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
        const auto& cursor = node.get_Processor().m_Cursor;
        if (cursor.m_hh.m_Height == Rules::get().pForks[1].m_Height) {
            BEAM_LOG_INFO () << "Reached fork 1...";
        }
        if (cursor.m_hh.m_Height == Rules::get().pForks[2].m_Height) {
            BEAM_LOG_INFO () << "Reached fork 2...";
            reactor->stop();
            return;
        }
        if (waitBlock && cursor.m_hh.m_Height == waitBlock) {
            BEAM_LOG_INFO () << "Reached block " << waitBlock << "...";
            reactor->stop();
            return;
        }
        if (cursor.m_hh.m_Height >= 100) {
            BEAM_LOG_INFO () << "Reached max allowed block...";
            WALLET_CHECK(!"Test should complete before block 100. Something went wrong.");
            reactor->stop();
            return;
        }
    });

    InitTestNode(node, r, receiverTreasury, &observer, receiverDB->get_MasterKdf());
    TestWalletRig receiver(receiverDB, stopReactor, TestWalletRig::RegularWithoutPoWBbs);

    auto ownerDB = createSqliteWalletDB("owner_wallet.db", false, true);
    TestWalletRig owner(ownerDB, stopReactor, TestWalletRig::RegularWithoutPoWBbs);

    BEAM_LOG_INFO() << "\nStarting node and waiting until fork2...";
    reactor->run();

    //
    // Here fork2 should be reached
    //
    auto cursor = node.get_Processor().m_Cursor;
    WALLET_CHECK(receiverDB->getTxHistory().empty());
    WALLET_CHECK(cursor.m_hh.m_Height > Rules::get().pForks[1].m_Height);
    WALLET_CHECK(cursor.m_hh.m_Height >= Rules::get().pForks[2].m_Height);

    //
    // And enough BEAM mined
    //
    storage::Totals totals(*receiverDB, false);
    const auto mined = AmountBig::get_Lo(totals.GetBeamTotals().Avail);
    BEAM_LOG_INFO() << "Beam mined " << PrintableAmount(mined);

    const auto deposit = Rules::get().CA.DepositForList2;
    const auto fee = Amount(100);
    const auto initial = Rules::get().CA.DepositForList2 * 2 + fee * 40; // 40 should be enough;

    BEAM_LOG_INFO() << "Beam necessary for test " << PrintableAmount(initial);
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
        storage::Totals allTotals(*ownerDB, false);

        auto availBM = AmountBig::get_Lo(allTotals.GetBeamTotals().Avail);
        auto availA1 = AmountBig::get_Lo(allTotals.GetTotals(ASSET1_ID).Avail);
        auto availA2 = AmountBig::get_Lo(allTotals.GetTotals(ASSET2_ID).Avail);

        WALLET_CHECK( availBM == beam);
        WALLET_CHECK(availA1 == asset1);
        WALLET_CHECK(availA2 == asset2);
    };

    checkOwnerTotals(0, 0, 0);
    beam::wallet::RegisterAllAssetCreators(*receiver.m_Wallet);
    beam::wallet::RegisterAllAssetCreators(*owner.m_Wallet);

    const auto getTx = [&](const IWalletDB::Ptr& db, TxID txid) -> auto {
      const auto otx = db->getTx(txid);
      WALLET_CHECK(otx.is_initialized());
      return otx.is_initialized() ? *otx : TxDescription();
    };

    TxDescription tx;
    auto runTest = [&](const char* name, const std::function<TxID ()>& test, int wcnt = 1, bool owner = true) {
        BEAM_LOG_INFO() << "\nTesting " << name << "...";

        helpers::StopWatch sw;
        sw.start();
        waitCount = wcnt;
        const auto txid = test();
        reactor->run();
        sw.stop();
        BEAM_LOG_INFO() << name << ", elapsed time: " << sw.milliseconds() << "ms";

        auto db = owner ? ownerDB : receiverDB;
        tx = getTx(db, txid);
        WALLET_CHECK(tx.m_txId == txid);
    };

    //
    // Assets flag not set, fail any asset tx
    //
    runTest("assets flag is false", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetReg)
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
            .SetParameter(TxParameterID::MyAddr, receiver.m_BbsAddr)
            .SetParameter(TxParameterID::PeerAddr, owner.m_BbsAddr));
    }, 2, false);
    BEAM_LOG_INFO() << "Now owner has " << PrintableAmount(storage::Totals(*ownerDB, false).GetBeamTotals().Avail);

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
            .SetParameter(TxParameterID::Fee, beam::Amount(100)));
    });
    WALLET_CHECK(tx.m_status        == TxStatus::Failed);
    WALLET_CHECK(tx.m_failureReason == TxFailureReason::FailedToGetParameter);

    // empty meta
    runTest("register, empty meta", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetReg)
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
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetMetadata, ASSET1_META));
    });

    currBM -= deposit + fee;
    WALLET_CHECK(tx.m_status      == TxStatus::Completed);
    WALLET_CHECK(tx.m_fee         == fee);
    WALLET_CHECK(tx.m_assetId     == ASSET1_ID);
    WALLET_CHECK(tx.m_peerAddr    == Zero);
    WALLET_CHECK(tx.m_myAddr      == Zero);
    WALLET_CHECK(tx.m_assetMeta   == ASSET1_META);
    checkOwnerTotals(currBM, currA1, currA2);

    // second time register the same asset
    runTest("register asset #1 second time", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetReg)
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
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetMetadata, ASSET2_META));
    });

    currBM -= deposit + fee;
    checkOwnerTotals(currBM, currA1, currA2);
    WALLET_CHECK(tx.m_status      == TxStatus::Completed);
    WALLET_CHECK(tx.m_fee         == fee);
    WALLET_CHECK(tx.m_assetId     == ASSET2_ID);
    WALLET_CHECK(tx.m_peerAddr    == Zero);
    WALLET_CHECK(tx.m_myAddr      == Zero);
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
        std::string meta2;
        info.m_Metadata.get_String(meta2);
        WALLET_CHECK(meta2 == meta);
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
    checkOwnerTotals(currBM, currA1, currA2);

    // send locked asset, tx should fail on SENDER size, thus only 1 transaction wait
    runTest("send locked asset", [&] {
        return owner.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
            .SetParameter(TxParameterID::Amount, currA1)
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetID, ASSET1_ID)
            .SetParameter(TxParameterID::MyAddr, owner.m_BbsAddr)
            .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr));
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
            .SetParameter(TxParameterID::MyAddr, owner.m_BbsAddr)
            .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr));
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
            .SetParameter(TxParameterID::MyAddr, owner.m_BbsAddr)
            .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr));
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
            .SetParameter(TxParameterID::MyAddr, receiver.m_BbsAddr)
            .SetParameter(TxParameterID::PeerAddr, owner.m_BbsAddr));
    }, 2, false);

    WALLET_CHECK(tx.m_status == TxStatus::Completed);
    WALLET_CHECK(tx.m_assetId == ASSET1_ID);
    checkOwnerTotals(currBM, currA1, currA2);

    // unregister used asset
    runTest("unregister used asset", [&] {
        return owner.m_Wallet->StartTransaction(CreateTransactionParameters(TxType::AssetUnreg)
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
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetMetadata, ASSET2_META));
    });

    WALLET_CHECK(tx.m_status == TxStatus::Completed);
    checkOwnerTotals(currBM, 0, 0);

    //
    // At last we're done!
    //
    BEAM_LOG_INFO() << "Finished testing assets...";
}

void TestAssetMetaErrors()
{
    BEAM_LOG_INFO() << "\nTesting asset metadata error messages...";

    const auto check = [](const std::string& strMeta, bool v5, bool v6, bool isStd, const std::string& expectedError)
    {
        const WalletAssetMeta meta(strMeta);
        BEAM_LOG_INFO() << "\tmeta: " << (strMeta.size() > 80 ? strMeta.substr(0, 80) + "..." : strMeta)
                        << " -> " << (meta.GetParseError().empty() ? "OK" : meta.GetParseError());

        WALLET_CHECK(meta.isStd_v5_0() == v5);
        WALLET_CHECK(meta.isStd_v6_0() == v6);
        WALLET_CHECK(meta.isStd() == isStd);
        WALLET_CHECK(meta.GetParseError() == expectedError);
    };

    const std::string ok = "STD:SCH_VER=1;N=Beam Coin;SN=BCN;UN=coin;NTHUN=groth";
    const std::string chars = "only letters, digits, spaces and \".,-_\" are allowed";

    // valid standard metadata, all optional fields present
    check(ok + ";OPT_SHORT_DESC=Short;OPT_LONG_DESC=Long;OPT_SITE_URL=https://beam.mw;OPT_PDF_URL=x.pdf;OPT_COLOR=#00FFaa",
          true, true, true, "");
    check(ok + ";OPT_COLOR=#0fA", true, true, true, "");
    check(ok + ";OPT_SHORT_DESC=" + std::string(128, 'a') + ";OPT_LONG_DESC=" + std::string(1024, 'b'), true, true, true, "");
    check("STD:SCH_VER=1;N=Beam Coin;SN=BCNBCN;UN=coin;NTHUN=groth", true, true, true, "");

    // old (v5.0) metadata: still accepted where old metadata is allowed, but not fully standard
    check("STD:N=Beam Coin;SN=BCN;UN=coin;NTHUN=groth", true, false, false, "required field SCH_VER (schema version) is missing");

    // not a standard metadata at all
    check("N=Beam Coin;SN=BCN;UN=coin;NTHUN=groth", false, false, false, "metadata must start with \"STD:\"");
    check("", false, false, false, "metadata must start with \"STD:\"");

    // missing required fields
    check("STD:SCH_VER=1;SN=BCN;UN=coin;NTHUN=groth", false, false, false, "required field N (asset name) is missing");
    check("STD:SCH_VER=1;N=Beam Coin;UN=coin;NTHUN=groth", false, false, false, "required field SN (short name) is missing");
    check("STD:SCH_VER=1;N=Beam Coin;SN=BCN;NTHUN=groth", false, false, false, "required field UN (unit name) is missing");
    check("STD:SCH_VER=1;N=Beam Coin;SN=BCN;UN=coin", false, false, false, "required field NTHUN (smallest unit name) is missing");
    check("STD:SCH_VER=1;N;SN=BCN;UN=coin;NTHUN=groth", false, false, false, "required field N (asset name) is missing");

    // bad characters in required fields
    check("STD:SCH_VER=1;N=Beam Coin!;SN=BCN;UN=coin;NTHUN=groth", false, false, false, "N (asset name) contains invalid character '!', " + chars);
    check("STD:SCH_VER=1;N=Beam Coin;SN=B$N;UN=coin;NTHUN=groth", false, false, false, "SN (short name) contains invalid character '$', " + chars);
    check("STD:SCH_VER=1;N=Beam Coin;SN=BCN;UN=co/in;NTHUN=groth", false, false, false, "UN (unit name) contains invalid character '/', " + chars);
    check("STD:SCH_VER=1;N=Beam Coin;SN=BCN;UN=coin;NTHUN=gro=th", false, false, false, "NTHUN (smallest unit name) contains invalid character '=', " + chars);
    check(std::string("STD:SCH_VER=1;N=Beam\t") + "Coin;SN=BCN;UN=coin;NTHUN=groth", false, false, false, "N (asset name) contains invalid character 0x09, " + chars);

    // schema version
    check("STD:SCH_VER=2;N=Beam Coin;SN=BCN;UN=coin;NTHUN=groth", true, false, false, "unsupported SCH_VER (schema version) \"2\", must be 1");
    check("STD:SCH_VER=one;N=Beam Coin;SN=BCN;UN=coin;NTHUN=groth", true, false, false, "unsupported SCH_VER (schema version) \"one\", must be 1");

    // optional fields
    check(ok + ";OPT_SHORT_DESC=" + std::string(129, 'a'), true, false, false, "OPT_SHORT_DESC is too long: 129 characters, maximum is 128");
    check(ok + ";OPT_LONG_DESC=" + std::string(1025, 'b'), true, false, false, "OPT_LONG_DESC is too long: 1025 characters, maximum is 1024");
    check(ok + ";OPT_COLOR=red", true, false, false, "OPT_COLOR \"red\" is invalid, must be a hex color like #RGB or #RRGGBB");
    check(ok + ";OPT_COLOR=#12345", true, false, false, "OPT_COLOR \"#12345\" is invalid, must be a hex color like #RGB or #RRGGBB");
    check(ok + ";OPT_COLOR=#" + std::string(40, 'Z'), true, false, false,
          "OPT_COLOR \"#" + std::string(31, 'Z') + "...\" is invalid, must be a hex color like #RGB or #RRGGBB");

    // empty required fields and short name length
    check("STD:SCH_VER=1;N=;SN=BCN;UN=coin;NTHUN=groth", true, true, false, "N (asset name) must not be empty");
    check("STD:SCH_VER=1;N=Beam Coin;SN=;UN=coin;NTHUN=groth", true, true, false, "SN (short name) must not be empty");
    check("STD:SCH_VER=1;N=Beam Coin;SN=BCN;UN=;NTHUN=groth", true, true, false, "UN (unit name) must not be empty");
    check("STD:SCH_VER=1;N=Beam Coin;SN=BCN;UN=coin;NTHUN=", true, true, false, "NTHUN (smallest unit name) must not be empty");
    check("STD:SCH_VER=1;N=Beam Coin;SN=BEAMCN1;UN=coin;NTHUN=groth", true, true, false, "SN (short name) is too long: 7 characters, maximum is 6");

    // several problems: the first one (in validation order) is reported
    check("STD:SCH_VER=2;SN=TOOLONGNAME;UN=coin;NTHUN=groth;OPT_COLOR=red", false, false, false, "required field N (asset name) is missing");
    check("STD:SCH_VER=2;N=;SN=TOOLONGNAME;UN=coin;NTHUN=groth;OPT_COLOR=red", true, false, false, "unsupported SCH_VER (schema version) \"2\", must be 1");

    // metadata taken from the asset info
    {
        Asset::Full info;
        const WalletAssetMeta meta(info);
        WALLET_CHECK(!meta.isStd());
        WALLET_CHECK(meta.GetParseError() == "metadata is empty");
    }
    {
        Asset::Full info;
        info.m_Metadata.set_String(ok, false);
        const WalletAssetMeta meta(info);
        WALLET_CHECK(meta.isStd());
        WALLET_CHECK(meta.GetParseError().empty());
    }

    BEAM_LOG_INFO() << "Finished testing asset metadata error messages...";
}

thread_local const beam::Rules* beam::Rules::s_pInstance = nullptr;

int main () {
    const auto logLevel = BEAM_LOG_LEVEL_DEBUG;
    const auto logger = beam::Logger::create(logLevel, logLevel);
    BEAM_LOG_INFO() << "Assets test - starting";

    beam::Rules r;
    beam::Rules::Scope scopeRules(r);

    WALLET_CHECK(r.CA.LockPeriod == r.MaxRollback);

    TestAssetMetaErrors();

    r.CA.Enabled          = true;
    r.CA.LockPeriod       = 20;
    r.CA.DepositForList2  = Rules::Coin * 1000;
    r.MaxRollback         = 20;
    r.m_Consensus         = Rules::Consensus::FakePoW;
    r.pForks[1].m_Height  = 5;
    r.pForks[2].m_Height  = 10;
    r.UpdateChecksum();

    TestAssets(r);

    BEAM_LOG_INFO() << "Assets test - completed, failures: " << g_failureCount;
    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}
