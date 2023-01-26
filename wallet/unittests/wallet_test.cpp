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
#define BEAM_BUILD_THREAD_POOL
#include "wallet/core/common.h"
#include "wallet/core/common_utils.h"
#include "wallet/core/wallet_network.h"
#include "wallet/core/wallet.h"
#include "wallet/core/secstring.h"
#include "wallet/core/base58.h"
#include "wallet/core/contracts/shaders_manager.h"
#include "wallet/client/wallet_client.h"
#include "utility/test_helpers.h"
#include "core/radixtree.h"
#include "core/unittest/mini_blockchain.h"
#include "core/negotiator.h"
#include "node/node.h"
#include "wallet/core/private_key_keeper.h"
#include "keykeeper/local_private_key_keeper.h"
#include "utility/hex.h"
#include "bvm/ManagerStd.h"
#include "wallet_test_node.h"
#include "mnemonic/mnemonic.h"
#include "bvm/bvm2.h"

#include "test_helpers.h"

#include <string_view>
#include <assert.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include "utility/thread.h"

#include "core/proto.h"
#include <boost/intrusive/list.hpp>

#if defined(BEAM_HW_WALLET)
#include "keykeeper/hw_wallet.h"
#endif

using namespace beam;
using namespace std;
using namespace ECC;

WALLET_TEST_INIT

#include "wallet_test_environment.cpp"
#include "wallet/api/v6_0/v6_api.h"

namespace
{
    struct AtomicSwapProvider : ISwapsProvider
    {
    public:
        Amount getCoinAvailable(AtomicSwapCoin swapCoin) const override { throw std::runtime_error("not impl"); }
        Amount getRecommendedFeeRate(AtomicSwapCoin swapCoin) const override { throw std::runtime_error("not impl"); }
        Amount getMinFeeRate(AtomicSwapCoin swapCoin) const override { throw std::runtime_error("not impl"); }
        Amount getMaxFeeRate(AtomicSwapCoin swapCoin) const override { throw std::runtime_error("not impl"); }
        const SwapOffersBoard& getSwapOffersBoard() const override { throw std::runtime_error("not impl"); }
        bool isCoinClientConnected(AtomicSwapCoin swapCoin) const override { throw std::runtime_error("not impl"); }
    };

    struct ApiTest
        : public IWalletApiHandler
        , public beam::wallet::V6Api
    {
        ApiTest(const ApiInitData& data)
            : V6Api(*this, data)
        {
        }

        std::vector<json> m_Messages;
        void sendAPIResponse(const json& msg) override
        {
            m_Messages.push_back(msg);
        }

        void TestTxListResSize(size_t s)
        {
            WALLET_CHECK(m_Messages.size() == 1);
            WALLET_CHECK(m_Messages[0]["result"].size() == s);
        }

        void TestTxListResHeight(Height h)
        {
            WALLET_CHECK(m_Messages.size() == 1);
            WALLET_CHECK(m_Messages[0]["result"][0]["height"] == h);
        }
    };

    void TestTxList()
    {
        cout << "\nTesting Tx list...\n";

        {
            std::vector<int> v = { 1, 2, 3, 4 };
            V6Api::doPagination(0, 0, v); // for count 0 we return all
            WALLET_CHECK(v.size() == 4); 
        }
        {
            std::vector<int> v = { 1, 2, 3, 4 };
            V6Api::doPagination(0, 3, v);
            WALLET_CHECK(v == std::vector<int>({1,2,3}));
        }
        {
            std::vector<int> v = { 1, 2, 3, 4 };
            V6Api::doPagination(1, 3, v);
            WALLET_CHECK(v == std::vector<int>({ 2,3,4 }));
        }
        {
            std::vector<int> v = { 1, 2, 3, 4 };
            V6Api::doPagination(2, 3, v);
            WALLET_CHECK(v == std::vector<int>({ 3,4 }));
        }
        {
            std::vector<int> v = { 1, 2, 3, 4 };
            V6Api::doPagination(1, 2, v);
            WALLET_CHECK(v == std::vector<int>({ 2,3 }));
        }
        {
            std::vector<int> v = { 1, 2, 3, 4 };
            V6Api::doPagination(4, 3, v);
            WALLET_CHECK(v.empty());
        }
        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);
        constexpr int Count = 64;
        int completedCount = 2 * Count;
        auto f = [&](auto)
        {
            --completedCount;
            if (completedCount == 0)
            {
                mainReactor->stop();
                completedCount = 2 * Count;
            }
        };

        TestNode node;
        TestWalletRig sender(createSenderWalletDB(Count, 5), f, TestWalletRig::Type::Regular, false, 0);
        TestWalletRig receiver(createReceiverWalletDB(), f);

        helpers::StopWatch sw;
        sw.start();

        Timestamp createTime = getTimestamp();
        for (int i = 0; i < Count; ++i)
        {
            sender.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
                .SetParameter(TxParameterID::MyAddr, sender.m_BbsAddr)
                .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr)
                .SetParameter(TxParameterID::Amount, Amount(1))
                .SetParameter(TxParameterID::Fee, Amount(2))
                .SetParameter(TxParameterID::Lifetime, Height(200))
                .SetParameter(TxParameterID::CreateTime, createTime));
            createTime += 1000;
        }
        
        mainReactor->run();
        sw.stop();


        // check coins
        vector<Coin> newSenderCoins = sender.GetCoins();
        vector<Coin> newReceiverCoins = receiver.GetCoins();

        //WALLET_CHECK(newSenderCoins.size() == Count);
        WALLET_CHECK(newReceiverCoins.size() == Count);

        // Tx history check
        auto sh = sender.m_WalletDB->getTxHistory();
        WALLET_CHECK(sh.size() == Count);
        auto rh = receiver.m_WalletDB->getTxHistory();
        WALLET_CHECK(rh.size() == Count);

        auto asp = std::make_shared<AtomicSwapProvider>();

        ApiInitData data;
        data.walletDB = sender.m_WalletDB;
        data.wallet = sender.m_Wallet;
        data.swaps = asp;

        ApiTest api(data);
        TxList message;
        message.count = 10;
        message.skip = 30;
        message.filter.status = wallet::TxStatus::Completed;
        sw.start();
        for (int i = 0; i < 100; ++i)
        {
            api.onHandleTxList(1, TxList(message));
            api.TestTxListResSize(10);
            api.m_Messages.clear();
        }
        sw.stop();
        cout << "TxList  elapsed time: " << sw.milliseconds() << " ms\n";

        message.count = 10;
        message.skip = 0;
        api.onHandleTxList(1, TxList(message));
        api.TestTxListResSize(10);
        api.m_Messages.clear();

        message.count = 100;
        message.skip = 0;
        api.onHandleTxList(1, TxList(message));
        api.TestTxListResSize(64);
        api.m_Messages.clear();

        message.count = 100;
        message.skip = 10;
        api.onHandleTxList(1, TxList(message));
        api.TestTxListResSize(54);
        api.m_Messages.clear();

        message.count = 10;
        message.skip = 10;
        api.onHandleTxList(1, TxList(message));
        api.TestTxListResSize(10);
        api.m_Messages.clear();

        message.count = 10;
        message.skip = 63;
        api.onHandleTxList(1, TxList(message));
        api.TestTxListResSize(1);
        api.m_Messages.clear();

        message.count = 10;
        message.skip = 64;
        api.onHandleTxList(1, TxList(message));
        api.TestTxListResSize(0);
        api.m_Messages.clear();

        message.count = 10;
        message.skip = 65;
        api.onHandleTxList(1, TxList(message));
        api.TestTxListResSize(0);
        api.m_Messages.clear();

        Timestamp t = std::numeric_limits<Timestamp>::max();
        int count = 0;

        std::map<TxID, Height> storedTx;
        sender.m_WalletDB->visitTx([&](const auto& tx)
        {
            const auto height = storage::DeduceTxProofHeight(*sender.m_WalletDB, tx);
            storedTx.emplace(tx.m_txId, height);
            WALLET_CHECK(tx.m_createTime > 0);
            WALLET_CHECK(tx.m_createTime < t);
            t = tx.m_createTime;
            ++count;
            return true;
        }, {});
        WALLET_CHECK(count == Count);
        WALLET_CHECK(count == (int)storedTx.size());
        for (auto p : storedTx)
        {

            TxList message2;
            message2.count = 0;
            message2.skip = 0;
            message2.filter.status = wallet::TxStatus::Completed;
            message2.filter.height = p.second;

            api.onHandleTxList(1, std::move(message2));
            api.TestTxListResSize(1);
            api.TestTxListResHeight(p.second);

            api.m_Messages.clear();
        }
    }

    void TestEventTypeSerialization()
    {
        std::string serializedStr;
        {
            proto::Event::Type::Enum event = proto::Event::Type::Shielded;

            Serializer ser;
            ser& event;

            ByteBuffer buf;
            ser.swap_buf(buf);

            serializedStr = beam::to_hex(&buf[0], buf.size());
            LOG_DEBUG() << "serialized proto::Event::Type::Shielded = 0x" << serializedStr;
        }
        
        auto f = [](const std::string& s, bool shouldThrow)
        {
            ByteBuffer buf2 = from_hex(s);

            Deserializer der;
            der.reset(buf2.data(), buf2.size());

            proto::Event::Type::Enum event;
            if (shouldThrow)
            {
                WALLET_CHECK_THROW(der & event);
            }
            else
            {
                WALLET_CHECK_NO_THROW(der & event);
                WALLET_CHECK(proto::Event::Type::Shielded == event);
            }

            der.reset(buf2.data(), buf2.size());
        };

        // legacy case
        f("42", true);
        // Normal case
        f("82", false);
    }

    void TestWalletNegotiation(IWalletDB::Ptr senderWalletDB, IWalletDB::Ptr receiverWalletDB)
    {
        cout << "\nTesting wallets negotiation...\n";

        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);

        WalletAddress wa;
        receiverWalletDB->createAddress(wa);
        receiverWalletDB->saveAddress(wa);
        WalletID receiver_id = wa.m_BbsAddr;
        PeerID receiver_Endpoint = wa.m_Endpoint;

        senderWalletDB->createAddress(wa);
        senderWalletDB->saveAddress(wa);
        WalletID sender_id = wa.m_BbsAddr;
        PeerID sender_Endpoint = wa.m_Endpoint;

        int count = 0;
        auto f = [&count](const auto& /*id*/)
        {
            if (++count >= 2)
                io::Reactor::get_Current().stop();
        };

        TestNodeNetwork::Shared tnns;

        Wallet sender(senderWalletDB, f);
        Wallet receiver(receiverWalletDB, f);

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
            .SetParameter(TxParameterID::MyAddr, sender_id)
            .SetParameter(TxParameterID::MyEndpoint, sender_Endpoint)
            .SetParameter(TxParameterID::PeerAddr, receiver_id)
            .SetParameter(TxParameterID::PeerEndpoint, receiver_Endpoint)
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

        auto senderWalletDB = createSqliteWalletDB("sender_wallet.db", false, false);

        // add coin with keyType - Coinbase
        beam::Amount coin_amount = 40;
        Coin coin = CreateAvailCoin(coin_amount, 0);
        coin.m_ID.m_Type = Key::Type::Coinbase;
        senderWalletDB->storeCoin(coin);

        vector<Coin> coins;
        vector<ShieldedCoin> shieldedCoins;
        senderWalletDB->selectCoins2(0, 24, Zero, coins, shieldedCoins, 0, false);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_ID.m_Type == Key::Type::Coinbase);
        WALLET_CHECK(coins[0].m_status == Coin::Available);
        WALLET_CHECK(senderWalletDB->getTxHistory().empty());

        TestNode node;
        TestWalletRig sender(senderWalletDB, [](auto) { io::Reactor::get_Current().stop(); });
        helpers::StopWatch sw;

        sw.start();

        auto txId = sender.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
                    .SetParameter(TxParameterID::MyAddr, sender.m_BbsAddr)
                    .SetParameter(TxParameterID::PeerAddr, sender.m_BbsAddr)
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
        WALLET_CHECK(txHistory[0].m_fee == 2);
        WALLET_CHECK(txHistory[0].m_status == wallet::TxStatus::Completed);

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

        WALLET_CHECK(newSenderCoins[1].m_ID.m_Type == Key::Type::Regular);
        WALLET_CHECK(newSenderCoins[1].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[1].m_ID.m_Value == 24);

        WALLET_CHECK(newSenderCoins[2].m_ID.m_Type == Key::Type::Change);
        WALLET_CHECK(newSenderCoins[2].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[2].m_ID.m_Value == 14);

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
        TestWalletRig sender(createSenderWalletDB(), f, TestWalletRig::Type::Regular, false, 0);
        TestWalletRig receiver(createReceiverWalletDB(), f);

        vector<Coin> coins;
        vector<ShieldedCoin> shieldedCoins;
        sender.m_WalletDB->selectCoins2(0, 6, Zero, coins, shieldedCoins, 0, false);
        WALLET_CHECK(coins.size() == 2);
        WALLET_CHECK(sender.m_WalletDB->getTxHistory().empty());
        WALLET_CHECK(receiver.m_WalletDB->getTxHistory().empty());

        helpers::StopWatch sw;
        sw.start();

        auto txId = sender.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
            .SetParameter(TxParameterID::MyAddr, sender.m_BbsAddr)
            .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr)
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
        WALLET_CHECK(stx->m_status == wallet::TxStatus::Completed);
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

            proto::FlyClient& flyClient = *receiver.m_Wallet;
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

            WALLET_CHECK(rtx->m_status == wallet::TxStatus::Completed);
            WALLET_CHECK(rtx->m_sender == false);
        }

        // second transfer
        vector<Coin> preselectedCoins;
        sender.m_WalletDB->selectCoins2(0, 6, Zero, preselectedCoins, shieldedCoins, 0, false);
        CoinIDList preselectedIDs;
        for (const auto& c : preselectedCoins)
        {
            preselectedIDs.push_back(c.m_ID);
        }
        

        cout << "An attempt to send from invalid address\n";
        WALLET_CHECK_THROW(txId = sender.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
            .SetParameter(TxParameterID::MyAddr, receiver.m_BbsAddr)
            .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr)
            .SetParameter(TxParameterID::Amount, Amount(6))
            .SetParameter(TxParameterID::Fee, Amount(0))
            .SetParameter(TxParameterID::Lifetime, Height(200))
            .SetParameter(TxParameterID::PreselectedCoins, preselectedIDs)));

        sw.start();

        txId = sender.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
            .SetParameter(TxParameterID::MyAddr, sender.m_BbsAddr)
            .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr)
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
        WALLET_CHECK(stx->m_status == wallet::TxStatus::Completed);
        WALLET_CHECK(stx->m_message == rtx->m_message);
        WALLET_CHECK(stx->m_createTime <= rtx->m_createTime);
        WALLET_CHECK(stx->m_status == rtx->m_status);
        WALLET_CHECK(stx->m_sender == true);
        WALLET_CHECK(rtx->m_sender == false);


        // third transfer. no enough money should appear
        sw.start();
        completedCount = 1;// only one wallet takes part in tx

        txId = sender.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
            .SetParameter(TxParameterID::MyAddr, sender.m_BbsAddr)
            .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr)
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
        WALLET_CHECK(stx->m_status == wallet::TxStatus::Failed);
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
            TestWalletRig sender(senderDB, f, TestWalletRig::Type::Regular, false, 0);
            TestWalletRig receiver(createReceiverWalletDB(), f);

            sender.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
                .SetParameter(TxParameterID::MyAddr, sender.m_BbsAddr)
                .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr)
                .SetParameter(TxParameterID::Amount, Amount(4))
                .SetParameter(TxParameterID::Fee, Amount(2))
                .SetParameter(TxParameterID::Lifetime, Height(200))
                .SetParameter(TxParameterID::PeerResponseTime, Height(20)));

            mainReactor->run();

            Block::SystemState::Full tip;
            sender.m_WalletDB->get_History().get_Tip(tip);
            sender.m_WalletDB->get_History().DeleteFrom(tip.m_Height);

            proto::FlyClient& client = *sender.m_Wallet;
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
        TestWalletRig sender(senderDB, f, TestWalletRig::Type::Regular, false, 0);
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

    void TestBbsDecrypt()
    {
        constexpr size_t AddressNum = 10000;
        constexpr size_t MessageSize = 1024;
        constexpr size_t ChannelsNum = 1;
        constexpr size_t MessageNum = 10;
        struct Address
        {
            ECC::Scalar::Native m_sk;
            PeerID m_pk;
        };

        std::vector<Address> addresses;
        addresses.reserve(AddressNum);

        ECC::Key::IKdf::Ptr kdf, senderKdf;
        ECC::HKdf::Create(kdf, unsigned(123));
        ECC::HKdf::Create(senderKdf, unsigned(3256));

        for (size_t i = 1; i < AddressNum +1; ++i)
        {
            ECC::Hash::Value v;
            ECC::Hash::Processor() << i >> v;
            auto& address = addresses.emplace_back();
            kdf->DeriveKey(address.m_sk, v);
            address.m_pk.FromSk(address.m_sk);
        }

        ByteBuffer message;
        message.reserve(MessageSize);
        std::generate_n(std::back_inserter(message), MessageSize, []() { return static_cast<uint8_t>(std::rand() % 255); });

        {
            cout << "Good messages...\n";
            std::vector<ByteBuffer> messages;
            messages.reserve(MessageNum);

            for (const auto& addr : addresses)
            {
                ECC::NoLeak<ECC::Hash::Value> hvRandom;
                ECC::GenRandom(hvRandom.V);

                ECC::Scalar::Native nonce;
                senderKdf->DeriveKey(nonce, hvRandom.V);

                ByteBuffer& encryptedMessage = messages.emplace_back();
                WALLET_CHECK(proto::Bbs::Encrypt(encryptedMessage, addr.m_pk, nonce, &message[0], static_cast<uint32_t>(message.size())));
            }
 
            cout << "starting...\n";
            helpers::StopWatch sw;
            sw.start();
            for (size_t m = 0; m < MessageNum; ++m)
            {
                for (size_t c = 0; c < ChannelsNum; ++c)
                {
                    //bool decrypted = false;
                    for (const auto& addr : addresses)
                    {
                        ByteBuffer buf = messages[m]; // duplicate
                        uint8_t* pMsg = &buf.front();
                        uint32_t nSize = static_cast<uint32_t>(buf.size());
                        if (proto::Bbs::Decrypt(pMsg, nSize, addr.m_sk))
                        {
                         //   decrypted = true;
                            break;
                        }
                    }
                    //WALLET_CHECK(decrypted);
                }
            }
            sw.stop();

            cout << "SBBS elapsed time: " << sw.milliseconds() 
                 << " ms\nAddress num: " << AddressNum 
                 << "\nMessage count: " << MessageNum
                 << "\nMessage size: " << MessageSize 
                 << " bytes\nChannels num: " << ChannelsNum << '\n';
        }

        {
            cout << "Bad messages...\n";

            ECC::Hash::Value v;
            ECC::Hash::Processor() << unsigned(2) << unsigned(63) >> v;
            Address otherAddress;
            kdf->DeriveKey(otherAddress.m_sk, v);
            otherAddress.m_pk.FromSk(otherAddress.m_sk);

            std::vector<ByteBuffer> messages;
            messages.reserve(MessageNum);

            for (size_t m = 0; m < MessageNum; ++m)
            {
                ECC::NoLeak<ECC::Hash::Value> hvRandom;
                ECC::GenRandom(hvRandom.V);

                ECC::Scalar::Native nonce;
                senderKdf->DeriveKey(nonce, hvRandom.V);

                ByteBuffer& encryptedMessage = messages.emplace_back();
                WALLET_CHECK(proto::Bbs::Encrypt(encryptedMessage, otherAddress.m_pk, nonce, &message[0], static_cast<uint32_t>(message.size())));
            }

            helpers::StopWatch sw;
            sw.start();
            for (size_t m = 0; m < MessageNum; ++m)
            {
                for (size_t c = 0; c < ChannelsNum; ++c)
                {
                    for (const auto& addr : addresses)
                    {
                        ByteBuffer buf = messages[m]; // duplicate
                        uint8_t* pMsg = &buf.front();
                        uint32_t nSize = static_cast<uint32_t>(buf.size());
                        WALLET_CHECK(!proto::Bbs::Decrypt(pMsg, nSize, addr.m_sk));
                    }
                }
            }
            sw.stop();

            cout << "SBBS elapsed time: " << sw.milliseconds()
                 << " ms\nAddress num: " << AddressNum
                 << "\nMessage count: " << MessageNum
                 << "\nMessage size: " << MessageSize
                 << " bytes\nChannels num: " << ChannelsNum << '\n';
        }
    }

    void TestSplitTransaction()
    {
        cout << "\nTesting split Tx...\n";

        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);

        auto senderWalletDB = createSqliteWalletDB("sender_wallet.db", false, false);

        // add coin with keyType - Coinbase
        beam::Amount coin_amount = 40;
        Coin coin = CreateAvailCoin(coin_amount, 0);
        coin.m_ID.m_Type = Key::Type::Coinbase;
        senderWalletDB->storeCoin(coin);

        vector<Coin> coins;
        vector<ShieldedCoin> shieldedCoins;
        senderWalletDB->selectCoins2(0, 24, Zero, coins, shieldedCoins, 0, false);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_ID.m_Type == Key::Type::Coinbase);
        WALLET_CHECK(coins[0].m_status == Coin::Available);
        WALLET_CHECK(senderWalletDB->getTxHistory().empty());

        TestNode node;
        TestWalletRig sender(senderWalletDB, [](auto) { io::Reactor::get_Current().stop(); });
        helpers::StopWatch sw;

        sw.start();

        auto txId = sender.m_Wallet->StartTransaction(CreateSplitTransactionParameters(sender.m_BbsAddr, AmountList{ 11, 12, 13 })
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
        WALLET_CHECK(txHistory[0].m_fee == 2);
        WALLET_CHECK(txHistory[0].m_status == wallet::TxStatus::Completed);

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

        WALLET_CHECK(newSenderCoins[1].m_ID.m_Type == Key::Type::Regular);
        WALLET_CHECK(newSenderCoins[1].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[1].m_ID.m_Value == 11);

        WALLET_CHECK(newSenderCoins[2].m_ID.m_Type == Key::Type::Regular);
        WALLET_CHECK(newSenderCoins[2].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[2].m_ID.m_Value == 12);

        WALLET_CHECK(newSenderCoins[3].m_ID.m_Type == Key::Type::Regular);
        WALLET_CHECK(newSenderCoins[3].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[3].m_ID.m_Value == 13);

        WALLET_CHECK(newSenderCoins[4].m_ID.m_Type == Key::Type::Change);
        WALLET_CHECK(newSenderCoins[4].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[4].m_ID.m_Value == 2);

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

        auto senderWalletDB = createSqliteWalletDB("sender_wallet.db", false, false);

        // add coin with keyType - Coinbase
        Coin coin = CreateAvailCoin(100, 0);
        coin.m_ID.m_Type = Key::Type::Coinbase;
        senderWalletDB->storeCoin(coin);

        vector<Coin> coins;
        vector<ShieldedCoin> shieldedCoins;
        senderWalletDB->selectCoins2(0, 24, Zero, coins, shieldedCoins, 0, false);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_ID.m_Type == Key::Type::Coinbase);
        WALLET_CHECK(coins[0].m_status == Coin::Available);
        WALLET_CHECK(senderWalletDB->getTxHistory().empty());

        TestNode node;
        TestWalletRig sender(senderWalletDB, [](auto) { io::Reactor::get_Current().stop(); });


        auto txId = sender.m_Wallet->StartTransaction(CreateSplitTransactionParameters(sender.m_BbsAddr, AmountList{ 11, 12, 13 })
           .SetParameter(TxParameterID::Fee, Amount(2))
           .SetParameter(TxParameterID::Lifetime, Height(200)));

        mainReactor->run();

        // check Tx
        {
            auto txHistory = senderWalletDB->getTxHistory();
            WALLET_CHECK(txHistory.size() == 1);
            WALLET_CHECK(txHistory[0].m_txId == txId);
            WALLET_CHECK(txHistory[0].m_amount == 36);
            WALLET_CHECK(txHistory[0].m_fee == 2);
            WALLET_CHECK(txHistory[0].m_status == wallet::TxStatus::Failed);
        }
        

        txId = sender.m_Wallet->StartTransaction(CreateSplitTransactionParameters(sender.m_BbsAddr, AmountList{ 11, 12, 13 })
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
            WALLET_CHECK(tx.m_fee == 42);
            WALLET_CHECK(tx.m_status == wallet::TxStatus::Failed);
        }

        // another attempt
        txId = sender.m_Wallet->StartTransaction(CreateSplitTransactionParameters(sender.m_BbsAddr, AmountList{ 11, 12, 13 })
            .SetParameter(TxParameterID::Fee, Amount(50))
            .SetParameter(TxParameterID::Lifetime, Height(200)));
        mainReactor->run();

        // check Tx
        {
            auto tx = senderWalletDB->getTx(txId);
            WALLET_CHECK(tx);
            WALLET_CHECK(tx->m_txId == txId);
            WALLET_CHECK(tx->m_amount == 36);
            WALLET_CHECK(tx->m_fee == 50);
            WALLET_CHECK(tx->m_status == wallet::TxStatus::Completed);
        }

        // check coins
        vector<Coin> newSenderCoins = sender.GetCoins();

        WALLET_CHECK(newSenderCoins.size() == 5);
        WALLET_CHECK(newSenderCoins[0].m_ID.m_Type == Key::Type::Coinbase);
        WALLET_CHECK(newSenderCoins[0].m_status == Coin::Spent);
        WALLET_CHECK(newSenderCoins[0].m_ID.m_Value == 100);

        WALLET_CHECK(newSenderCoins[1].m_ID.m_Type == Key::Type::Regular);
        WALLET_CHECK(newSenderCoins[1].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[1].m_ID.m_Value == 11);

        WALLET_CHECK(newSenderCoins[2].m_ID.m_Type == Key::Type::Regular);
        WALLET_CHECK(newSenderCoins[2].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[2].m_ID.m_Value == 12);

        WALLET_CHECK(newSenderCoins[3].m_ID.m_Type == Key::Type::Regular);
        WALLET_CHECK(newSenderCoins[3].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[3].m_ID.m_Value == 13);

        WALLET_CHECK(newSenderCoins[4].m_ID.m_Type == Key::Type::Change);
        WALLET_CHECK(newSenderCoins[4].m_status == Coin::Available);
        WALLET_CHECK(newSenderCoins[4].m_ID.m_Value == 14);
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

        TestWalletRig sender(createSenderWalletDB(), f);
        TestWalletRig receiver(createReceiverWalletDB(), f, TestWalletRig::Type::Offline);

        auto newBlockFunc = [&receiver](Height height)
        {
            if (height == 200)
            {
                auto nodeEndpoint = make_shared<proto::FlyClient::NetworkStd>(*receiver.m_Wallet);
                nodeEndpoint->m_Cfg.m_vNodes.push_back(io::Address::localhost().port(32125));
                nodeEndpoint->Connect();
                receiver.m_Wallet->AddMessageEndpoint(make_shared<WalletNetworkViaBbs>(*receiver.m_Wallet, nodeEndpoint, receiver.m_WalletDB));
                receiver.m_Wallet->SetNodeEndpoint(nodeEndpoint);
            }
        };

        TestNode node(newBlockFunc);
        io::Timer::Ptr timer = io::Timer::create(*mainReactor);
        timer->start(1000, true, [&node]() {node.AddBlock(); });

        vector<Coin> coins;
        vector<ShieldedCoin> shieldedCoins;
        sender.m_WalletDB->selectCoins2(0, 6, Zero, coins, shieldedCoins, 0, false);
        WALLET_CHECK(coins.size() == 2);
        WALLET_CHECK(sender.m_WalletDB->getTxHistory().empty());
        WALLET_CHECK(receiver.m_WalletDB->getTxHistory().empty());

        auto txId = sender.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
            .SetParameter(TxParameterID::MyAddr, sender.m_BbsAddr)
            .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr)
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
            WALLET_CHECK(sh[0].m_status == wallet::TxStatus::Failed);
            WALLET_CHECK(sh[0].m_failureReason == TxFailureReason::TransactionExpired);
            auto rh = receiver.m_WalletDB->getTxHistory();
            WALLET_CHECK(rh.size() == 1);
            WALLET_CHECK(rh[0].m_status == wallet::TxStatus::Failed);
            WALLET_CHECK(rh[0].m_failureReason == TxFailureReason::TransactionExpired);
        }

        txId = sender.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
            .SetParameter(TxParameterID::MyAddr, sender.m_BbsAddr)
            .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr)
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
            WALLET_CHECK(sit->m_status == wallet::TxStatus::Completed);
            auto rh = receiver.m_WalletDB->getTxHistory();
            WALLET_CHECK(rh.size() == 2);
            auto rit = find_if(rh.begin(), rh.end(), [&txId](const auto& t) {return t.m_txId == txId; });
            WALLET_CHECK(rit->m_status == wallet::TxStatus::Completed);
        }
    }

    void TestNoResponse()
    {
        cout << "\nTesting no peer response...\n";

        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);

        int completedCount = 1;
        auto f = [&completedCount, mainReactor](auto)
        {
            --completedCount;
            if (completedCount == 0)
            {
                mainReactor->stop();
                completedCount = 1;
            }
        };
        TestWalletRig receiver(createReceiverWalletDB(), f, TestWalletRig::Type::Offline);
        auto senderDB = createSenderWalletDB();
        
        auto newBlockFunc = [](Height height)
        {
            if (height == 150)
            {
                io::Reactor::get_Current().stop();
            }
            if (height == 158)
            {
                WALLET_CHECK(!"Something went wrong");
                io::Reactor::get_Current().stop();
            }
        };

        TestNode node(newBlockFunc);
        io::Timer::Ptr timer = io::Timer::create(*mainReactor);
        timer->start(1000, true, [&node]() {node.AddBlock(); });
        {
            TestWalletRig sender(senderDB, f);
            vector<Coin> coins;
            vector<ShieldedCoin> shieldedCoins;
            sender.m_WalletDB->selectCoins2(0, 6, Zero, coins, shieldedCoins, 0, false);
            WALLET_CHECK(coins.size() == 2);
            WALLET_CHECK(sender.m_WalletDB->getTxHistory().empty());

            sender.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
                .SetParameter(TxParameterID::MyAddr, sender.m_BbsAddr)
                .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr)
                .SetParameter(TxParameterID::Amount, Amount(4))
                .SetParameter(TxParameterID::Fee, Amount(2))
                .SetParameter(TxParameterID::Lifetime, Height(0))
                .SetParameter(TxParameterID::PeerResponseTime, Height(10)));

            mainReactor->run();

            {
                vector<Coin> newSenderCoins = sender.GetCoins();
                vector<Coin> newReceiverCoins = receiver.GetCoins();

                WALLET_CHECK(newSenderCoins.size() == 4);
                WALLET_CHECK(newReceiverCoins.size() == 0);

                auto sh = sender.m_WalletDB->getTxHistory();
                WALLET_CHECK(sh.size() == 1);
                WALLET_CHECK(sh[0].m_status == wallet::TxStatus::InProgress);
                auto rh = receiver.m_WalletDB->getTxHistory();
                WALLET_CHECK(rh.size() == 0);
            }
        }
        {
            // Recreate wallet
            TestWalletRig sender(senderDB, f);
            mainReactor->run();
            vector<Coin> newSenderCoins = sender.GetCoins();
            vector<Coin> newReceiverCoins = receiver.GetCoins();

            WALLET_CHECK(newSenderCoins.size() == 4);
            WALLET_CHECK(newReceiverCoins.size() == 0);

            auto sh = sender.m_WalletDB->getTxHistory();
            WALLET_CHECK(sh.size() == 1);
            WALLET_CHECK(sh[0].m_status == wallet::TxStatus::Failed);
            WALLET_CHECK(sh[0].m_failureReason == TxFailureReason::TransactionExpired);
            auto rh = receiver.m_WalletDB->getTxHistory();
            WALLET_CHECK(rh.size() == 0);
        }
    }

    void TestTransactionUpdate()
    {
        cout << "\nTesting transaction update ...\n";

        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);
        EmptyTestGateway gateway;
        TestWalletRig sender(createSenderWalletDB());
        TestWalletRig receiver(createReceiverWalletDB());

        TxID txID = wallet::GenerateTxID();
        SimpleTransaction::Creator simpleCreator(sender.m_WalletDB);
        BaseTransaction::Creator& creator = simpleCreator;
        auto tx = creator.Create(BaseTransaction::TxContext(gateway, sender.m_WalletDB, txID));

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
        txDescription.m_peerAddr = receiver.m_BbsAddr;
        txDescription.m_myAddr = sender.m_BbsAddr;
        txDescription.m_message = {};
        txDescription.m_createTime = getTimestamp();
        txDescription.m_sender = true;
        txDescription.m_status = wallet::TxStatus::Pending;
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

        TestWalletRig sender(createSenderWalletDB());
        TestWalletRig receiver(createReceiverWalletDB());
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
            auto tx = txCreator.Create(BaseTransaction::TxContext(gateway, sender.m_WalletDB, txID));

            tx->SetParameter(wallet::TxParameterID::TransactionType, wallet::TxType::Simple, false);
            tx->SetParameter(wallet::TxParameterID::MaxHeight, currentHeight + 2, false); // transaction is valid +lifetime blocks from currentHeight
            tx->SetParameter(wallet::TxParameterID::IsInitiator, true, false);

            TxDescription txDescription(txID);

            txDescription.m_txId = txID;
            txDescription.m_amount = 1;
            txDescription.m_fee = 2;
            txDescription.m_minHeight = currentHeight;
            txDescription.m_peerAddr = receiver.m_BbsAddr;
            txDescription.m_myAddr = sender.m_BbsAddr;
            txDescription.m_message = {};
            txDescription.m_createTime = getTimestamp();
            txDescription.m_sender = true;
            txDescription.m_status = wallet::TxStatus::Pending;
            txDescription.m_selfTx = false;
            sender.m_WalletDB->saveTx(txDescription);

            tx->Update();

            auto result = sender.m_WalletDB->getTx(txID);

            WALLET_CHECK(result->m_status == wallet::TxStatus::Failed);
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
            auto tx = txCreator.Create(BaseTransaction::TxContext(gateway, sender.m_WalletDB, txID));

            tx->SetParameter(wallet::TxParameterID::TransactionType, wallet::TxType::Simple, false);
            tx->SetParameter(wallet::TxParameterID::MaxHeight, currentHeight + 2, false); // transaction is valid +lifetime blocks from currentHeight
            tx->SetParameter(wallet::TxParameterID::IsInitiator, true, false);

            TxDescription txDescription(txID);

            txDescription.m_txId = txID;
            txDescription.m_amount = 1;
            txDescription.m_fee = 2;
            txDescription.m_minHeight = currentHeight;
            txDescription.m_peerAddr = receiver.m_BbsAddr;
            txDescription.m_myAddr = sender.m_BbsAddr;
            txDescription.m_message = {};
            txDescription.m_createTime = getTimestamp();
            txDescription.m_sender = true;
            txDescription.m_status = wallet::TxStatus::Pending;
            txDescription.m_selfTx = false;
            sender.m_WalletDB->saveTx(txDescription);

            tx->Update();

            auto result = sender.m_WalletDB->getTx(txID);

            WALLET_CHECK(result->m_status == wallet::TxStatus::Failed);
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
            MyFlyClient(IWalletDB::Ptr db, const WalletID& receiverID)
                : m_Nnet(make_shared<proto::FlyClient::NetworkStd>(*this))
                , m_Bbs(*this, m_Nnet, db)
                , m_ReceiverID(receiverID)
            {
                WalletAddress wa;
                db->createAddress(wa);
                db->saveAddress(wa);
                m_BbsAddr = wa.m_BbsAddr;
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
                //endpoint.SendRawMessage(m_ReceiverID, message);
                SetTxParameter params;
                params.m_From = m_BbsAddr;
                params.m_Type = TxType::Simple;
                params.m_TxID = wallet::GenerateTxID();
                params.AddParameter(TxParameterID::Inputs, message);

                endpoint.Send(m_ReceiverID, params);
            }

            Block::SystemState::HistoryMap m_Headers;
            std::shared_ptr<proto::FlyClient::NetworkStd> m_Nnet;
            WalletNetworkViaBbs m_Bbs;
            WalletID m_ReceiverID;
            WalletID m_BbsAddr;
        };

        struct SenderFlyClient : public MyFlyClient
        {
            SenderFlyClient(IWalletDB::Ptr db, const WalletID& receiverID)
                : MyFlyClient(db, receiverID)
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
            ReceiverFlyClient(IWalletDB::Ptr db, const WalletID& receiverID)
                : MyFlyClient(db, receiverID)
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


        auto db = createSqliteWalletDB(SenderWalletDB, false, false);
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

        TestWalletRig receiver(createReceiverWalletDB(), [](auto) {});

        SenderFlyClient flyClient(db, receiver.m_BbsAddr);
        flyClient.Connect(senderNodeAddress);

        ReceiverFlyClient flyClinet2(receiver.m_WalletDB, flyClient.m_BbsAddr);
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

        auto db = createSqliteWalletDB(SenderWalletDB, false, false);
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

        TestWalletRig sender(db, f, TestWalletRig::Type::Regular, false, 0, senderNodeAddress);
        TestWalletRig receiver(createReceiverWalletDB(), f, TestWalletRig::Type::Regular, false, 0, receiverNodeAddress);

        sender.m_Wallet->StartTransaction(CreateSplitTransactionParameters(sender.m_BbsAddr, AmountList(Count, Amount(5)))
            .SetParameter(TxParameterID::Fee, Amount(0))
            .SetParameter(TxParameterID::Lifetime, Height(200)));

        mainReactor->run();

        sender.m_Wallet->SetBufferSize(10);

        completedCount = 2 * Count;

        for (int i = 0; i < Count; ++i)
        {
            sender.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
                .SetParameter(TxParameterID::MyAddr, sender.m_BbsAddr)
                .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr)
                .SetParameter(TxParameterID::Amount, Amount(4))
                .SetParameter(TxParameterID::Fee, Amount(1))
                .SetParameter(TxParameterID::Lifetime, Height(50))
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
            WALLET_CHECK(t.m_status == wallet::TxStatus::Completed);
        }
    }

    struct MyManager
        :public ManagerStdInWallet
    {
        bool m_Done = false;
        bool m_Err = false;
        bool m_Async = false;

        using ManagerStdInWallet::ManagerStdInWallet;

        void OnDone(const std::exception* pExc) override
        {
            m_Done = true;
            m_Err = !!pExc;

            if (m_Async)
                io::Reactor::get_Current().stop();
        }

        void get_Pk(ECC::Point& res, const Blob& d)
        {
            ECC::Point::Native pt;
            DerivePkInternal(pt, d);
            pt.Export(res);
        }

        static void Compile(ByteBuffer& res, const char* sz, Kind kind)
        {
            std::FStream fs;
            fs.Open(sz, true, true);

            res.resize(static_cast<size_t>(fs.get_Remaining()));
            if (!res.empty())
                fs.read(&res.front(), res.size());

            bvm2::Processor::Compile(res, res, kind);
        }

        void RunSync0(uint32_t iMethod)
        {
            m_Done = false;
            m_Err = false;
            m_Async = false;

            StartRun(iMethod);
        }

        void RunSync1()
        {
            if (m_Done)
                return;

            m_Async = true;
            io::Reactor::get_Current().run();

            if (!m_Done)
                // propagate it
                io::Reactor::get_Current().stop();
        }

        void RunSync(uint32_t iMethod)
        {
            RunSync0(iMethod);
            RunSync1();
        }

        bool StartTx(int& completedCount, TxID& txid)
        {
            if (m_InvokeData.m_vec.empty())
                return false;

            txid = m_pWallet->StartTransaction(
                CreateTransactionParameters(TxType::Contract)
                .SetParameter(TxParameterID::ContractDataPacked, m_InvokeData));

            completedCount++;
            return true;
        }

        bool DoTx(int& completedCount)
        {
            TxID txid;
            if (!StartTx(completedCount, txid))
                return false;

            io::Reactor::get_Current().run();
            auto pTx = m_pWalletDB->getTx(txid);
            return (pTx->m_status == wallet::TxStatus::Completed);
        }

        static bool get_OutpStrEx2(const std::string& s, const char* szKey, std::string& sOut, const uint32_t* pLen)
        {
            auto i = s.find(szKey, 0);
            if (s.npos == i)
                return false;

            i += strlen(szKey);

            if (pLen)
            {
                if (s.size() < i + *pLen)
                    return false;

                sOut.assign(s.c_str() + i, *pLen);
            }
            else
                sOut = s.c_str() + i;

            return true;
        }

        static bool get_OutpStrEx(const std::string& s, const char* szKey, std::string& sOut, uint32_t nLen)
        {
            return get_OutpStrEx2(s, szKey, sOut, &nLen);
        }

        static bool get_OutpStrEx(const std::string& s, const char* szKey, std::string& sOut)
        {
            return get_OutpStrEx2(s, szKey, sOut, nullptr);
        }

        bool get_OutpStr(const char* szKey, std::string& sOut, uint32_t nLen) const
        {
            return get_OutpStrEx(m_Out.str(), szKey, sOut, nLen);
        }

    };


    void TestAppShader1()
    {
        printf("Testing multi-wallet app shader with comm...\n");

        io::Reactor::Ptr mainReactor(io::Reactor::create());
        io::Reactor::Scope scope(*mainReactor);

        string nodePath = "node.db";
        if (boost::filesystem::exists(nodePath))
            boost::filesystem::remove(nodePath);

        int completedCount = 0;

        //auto timer = io::Timer::create(io::Reactor::get_Current());
        auto f = [&completedCount, mainReactor](auto txID)
        {
            --completedCount;
            if (completedCount == 0)
            {
              //  timer->cancel();
                mainReactor->stop();
            }
        };

        auto dbSender = createSqliteWalletDB(SenderWalletDB, false, true);
        auto dbReceiver = createReceiverWalletDB(false, true);
        auto treasury = createTreasury(dbSender, AmountList{Rules::Coin * 300000});

        auto nodeCreator = [](Node& node, const ByteBuffer& treasury, uint16_t port, const std::string& path, const std::vector<io::Address>& peers = {}, bool miningNode = true)->io::Address
        {
            InitNodeToTest(node, treasury, nullptr, port, 3000, path, peers, miningNode);
            io::Address address;
            address.resolve("127.0.0.1");
            address.port(port);
            return address;
        };


        Node node;
        auto nodeAddress = nodeCreator(node, treasury, 32125, "node.db");

        TestWalletRig sender(dbSender, f, TestWalletRig::Type::Regular, false, 0, nodeAddress);
        TestWalletRig receiver(dbReceiver, f, TestWalletRig::Type::Regular, false, 0, nodeAddress);


        MyManager manSender(dbSender, sender.m_Wallet);
        manSender.set_Privilege(2);
        MyManager::Compile(manSender.m_BodyManager, "vault/app.wasm", MyManager::Kind::Manager);
        MyManager::Compile(manSender.m_BodyContract, "vault/contract.wasm", MyManager::Kind::Contract);

        printf("Deploying vault...\n");

        manSender.m_Args["role"] = "manager";
        manSender.m_Args["action"] = "create";
        manSender.RunSync(1);
        WALLET_CHECK(manSender.m_Done && !manSender.m_Err);

        WALLET_CHECK(manSender.DoTx(completedCount));

        printf("reading vault cid...\n");

        manSender.m_Args["action"] = "view";
        manSender.RunSync(1);
        WALLET_CHECK(manSender.m_Done && !manSender.m_Err);

        std::string sCid;
        WALLET_CHECK(manSender.get_OutpStr("\"cid\": \"", sCid, bvm2::ContractID::nTxtLen));

        printf("getting vault keys...\n");
        std::string sKeySender, sKeyReceiver;

        manSender.m_Args["cid"] = sCid;
        manSender.m_Args["role"] = "my_account";
        manSender.m_Args["action"] = "get_key";
        manSender.RunSync(1);
        WALLET_CHECK(manSender.m_Done && !manSender.m_Err);
        WALLET_CHECK(manSender.get_OutpStr("\"key\": \"", sKeySender, sizeof(ECC::Point) * 2));

        MyManager manReceiver(dbReceiver, receiver.m_Wallet);
        manReceiver.set_Privilege(2);
        manReceiver.m_BodyManager = manSender.m_BodyManager;

        manReceiver.m_Args["cid"] = sCid;
        manReceiver.m_Args["role"] = "my_account";
        manReceiver.m_Args["action"] = "get_key";
        manReceiver.RunSync(1);
        WALLET_CHECK(manReceiver.m_Done && !manSender.m_Err);
        WALLET_CHECK(manReceiver.get_OutpStr("\"key\": \"", sKeyReceiver, sizeof(ECC::Point) * 2));

        printf("depositing to multi-user vault account...\n");

        manSender.m_Args["action"] = "deposit";
        manSender.m_Args["amount"] = "377000";
        manSender.m_Args["pkForeign"] = sKeyReceiver;

        manSender.RunSync(1);
        WALLET_CHECK(manSender.m_Done && !manSender.m_Err);

        WALLET_CHECK(manSender.DoTx(completedCount));

        printf("Withdrawing from multi-user vault account...\n");

        manSender.m_Args["action"] = "withdraw";
        manSender.m_Args["amount"] = "370000";
        manSender.m_Args["amountCoSigner"] = "100";
        manReceiver.m_Args["action"] = "withdraw";
        manReceiver.m_Args["amount"] = "370000";
        manReceiver.m_Args["amountCoSigner"] = "100";
        manReceiver.m_Args["pkForeign"] = sKeySender;
        manReceiver.m_Args["bCoSigner"] = "1";

        manReceiver.RunSync0(1);
        WALLET_CHECK(!manReceiver.m_Done); // must await for the sender

        manSender.RunSync0(1);
        WALLET_CHECK(!manSender.m_Done);

        manSender.RunSync1(); // wait synchronously

        WALLET_CHECK(manSender.m_Done && !manSender.m_Err);
        WALLET_CHECK(manReceiver.m_Done && !manReceiver.m_Err);

        TxID txS, txR;
        WALLET_CHECK(manSender.StartTx(completedCount, txS));
        WALLET_CHECK(manReceiver.StartTx(completedCount, txR));

        io::Reactor::get_Current().run();
        WALLET_CHECK(!completedCount);

        WALLET_CHECK(dbSender->getTx(txS)->m_status == wallet::TxStatus::Completed);
        WALLET_CHECK(dbReceiver->getTx(txR)->m_status == wallet::TxStatus::Completed);

    }



    void TestAppShader2()
    {
        printf("Testing upgradable2...\n");

        io::Reactor::Ptr mainReactor(io::Reactor::create());
        io::Reactor::Scope scope(*mainReactor);

        string nodePath = "node.db";
        if (boost::filesystem::exists(nodePath))
            boost::filesystem::remove(nodePath);

        int completedCount = 0;

        //auto timer = io::Timer::create(io::Reactor::get_Current());
        auto f = [&completedCount, mainReactor](auto txID)
        {
            --completedCount;
            if (completedCount == 0)
            {
              //  timer->cancel();
                mainReactor->stop();
            }
        };

        constexpr uint32_t nPeers = 5;
        constexpr uint32_t iSender = 3;
        wallet::IWalletDB::Ptr pDb[nPeers];
        for (uint32_t i = 0; i < nPeers; i++)
        {
            std::string sPath = std::string("sender_") + std::to_string(i) + "wallet.db";
            pDb[i] = createSqliteWalletDB(sPath, false, true);
        }

        auto treasury = createTreasury(pDb[iSender], AmountList{Rules::Coin * 300000});

        auto nodeCreator = [](Node& node, const ByteBuffer& treasury, uint16_t port, const std::string& path, const std::vector<io::Address>& peers = {}, bool miningNode = true)->io::Address
        {
            InitNodeToTest(node, treasury, nullptr, port, 3000, path, peers, miningNode);
            io::Address address;
            address.resolve("127.0.0.1");
            address.port(port);
            return address;
        };


        Node node;
        auto nodeAddress = nodeCreator(node, treasury, 32125, "node.db");

        std::unique_ptr<TestWalletRig> pRig[nPeers];
        std::unique_ptr<MyManager> pMan[nPeers];

        for (uint32_t i = 0; i < nPeers; i++)
        {
            pRig[i] = std::make_unique<TestWalletRig>(pDb[i], f, TestWalletRig::Type::Regular, false, 0, nodeAddress);
            pMan[i] = std::make_unique<MyManager>(pDb[i], pRig[i]->m_Wallet);
            pMan[i]->set_Privilege(2);

            if (i)
                pMan[i]->m_BodyManager = pMan[0]->m_BodyManager;
            else
                MyManager::Compile(pMan[i]->m_BodyManager, "upgradable2/Test/test_app.wasm", MyManager::Kind::Manager);
        }

        printf("Deploying v0\n");

        MyManager::Compile(pMan[iSender]->m_BodyContract, "upgradable2/Test/test_v0.wasm", MyManager::Kind::Contract);
        pMan[iSender]->m_Args["role"] = "manager";
        pMan[iSender]->m_Args["action"] = "deploy_version";
        pMan[iSender]->RunSync(1);
        WALLET_CHECK(pMan[iSender]->m_Done && !pMan[iSender]->m_Err);

        WALLET_CHECK(pMan[iSender]->DoTx(completedCount));

        printf("Deploying v1\n");

        MyManager::Compile(pMan[iSender]->m_BodyContract, "upgradable2/Test/test_v1.wasm", MyManager::Kind::Contract);
        pMan[iSender]->RunSync(1);
        WALLET_CHECK(pMan[iSender]->m_Done && !pMan[iSender]->m_Err);

        WALLET_CHECK(pMan[iSender]->DoTx(completedCount));

        printf("reading ver cids...\n");

        pMan[iSender]->m_Args["action"] = "view";
        pMan[iSender]->RunSync(1);
        WALLET_CHECK(pMan[iSender]->m_Done && !pMan[iSender]->m_Err);

        std::string sTmp, sV0Cid, sV1Cid;

        WALLET_CHECK(MyManager::get_OutpStrEx(pMan[iSender]->m_Out.str(), "\"Number\": 0", sTmp));
        WALLET_CHECK(MyManager::get_OutpStrEx(sTmp, "\"cid\": \"", sV0Cid, bvm2::ContractID::nTxtLen));

        WALLET_CHECK(MyManager::get_OutpStrEx(pMan[iSender]->m_Out.str(), "\"Number\": 1", sTmp));
        WALLET_CHECK(MyManager::get_OutpStrEx(sTmp, "\"cid\": \"", sV1Cid, bvm2::ContractID::nTxtLen));

        WALLET_CHECK(!sV0Cid.empty() && !sV1Cid.empty() && (sV0Cid != sV1Cid));

        printf("collecting keys...\n");

        std::string pAdminKey[nPeers];
        for (uint32_t i = 0; i < nPeers; i++)
        {
            pMan[i]->m_Args["role"] = "manager";
            pMan[i]->m_Args["action"] = "my_admin_key";
            pMan[i]->RunSync(1);
            WALLET_CHECK(pMan[i]->m_Done && !pMan[i]->m_Err);

            WALLET_CHECK(pMan[i]->get_OutpStr("\"admin_key\": \"", pAdminKey[i], sizeof(ECC::Point) * 2));
        }


        printf("Deploying upgradable2/v0\n");

        MyManager::Compile(pMan[iSender]->m_BodyContract, "upgradable2/contract.wasm", MyManager::Kind::Contract);
        pMan[iSender]->m_Args["action"] = "deploy_contract";
        pMan[iSender]->m_Args["cidVersion"] = sV0Cid;
        pMan[iSender]->m_Args["hUpgradeDelay"] = "1";
        pMan[iSender]->m_Args["nMinApprovers"] = std::to_string(nPeers);

        for (uint32_t i = 0; i < nPeers; i++)
            pMan[iSender]->m_Args[std::string("admin_") + std::to_string(i)] = pAdminKey[i];

        pMan[iSender]->RunSync(1);
        WALLET_CHECK(pMan[iSender]->m_Done && !pMan[iSender]->m_Err);

        WALLET_CHECK(pMan[iSender]->DoTx(completedCount));

        printf("reading cid...\n");

        pMan[iSender]->m_Args["action"] = "view";
        pMan[iSender]->RunSync(1);
        WALLET_CHECK(pMan[iSender]->m_Done && !pMan[iSender]->m_Err);

        std::string sCid;
        WALLET_CHECK(MyManager::get_OutpStrEx(pMan[iSender]->m_Out.str(), "\"contracts\":", sTmp));
        WALLET_CHECK(MyManager::get_OutpStrEx(sTmp, "\"cid\": \"", sCid, bvm2::ContractID::nTxtLen));

        printf("scheduling upgrade...\n");

        for (uint32_t i = 0; i < nPeers; i++)
        {
            pMan[i]->m_Args["action"] = "schedule_upgrade";
            pMan[i]->m_Args["cid"] = sCid;
            pMan[i]->m_Args["cidVersion"] = sV1Cid;
            pMan[i]->m_Args["hTarget"] = std::to_string(node.get_Processor().m_Cursor.m_Full.m_Height + 50);
            pMan[i]->m_Args["iSender"] = std::to_string(iSender);
            pMan[i]->m_Args["approve_mask"] = std::to_string((1U << nPeers) - 1);

            if (iSender == i)
            {
                pMan[i]->RunSync0(1);
                WALLET_CHECK(!pMan[i]->m_Done);
            }
        }

        for (uint32_t i = 0; i < nPeers; i++)
        {
            if (iSender != i)
            {
                pMan[i]->RunSync0(1);
                WALLET_CHECK(!pMan[i]->m_Done);
            }
        }

        pMan[iSender]->RunSync1(); // wait synchronously

        WALLET_CHECK(pMan[iSender]->m_Done && !pMan[iSender]->m_Err);
        WALLET_CHECK(pMan[iSender]->DoTx(completedCount));

        printf("checking state...\n");

        pMan[iSender]->m_Args["action"] = "view";
        pMan[iSender]->RunSync(1);
        WALLET_CHECK(pMan[iSender]->m_Done && !pMan[iSender]->m_Err);

        WALLET_CHECK(MyManager::get_OutpStrEx(pMan[iSender]->m_Out.str(), "\"next_version\":", sTmp));




        printf("Testing migration to upgradable3...\n");
        printf("Deploying upgradable2 cocoon\n");

        MyManager::Compile(pMan[iSender]->m_BodyContract, "upgradable3/Test/test_v0_migrate.wasm", MyManager::Kind::Contract);
        pMan[iSender]->m_Args["action"] = "deploy_version";

        pMan[iSender]->RunSync(1);
        WALLET_CHECK(pMan[iSender]->m_Done && !pMan[iSender]->m_Err);

        WALLET_CHECK(pMan[iSender]->DoTx(completedCount));

        bvm2::ShaderID sidCocoon;
        bvm2::get_ShaderID(sidCocoon, pMan[iSender]->m_BodyContract);
        bvm2::ContractID cidCocoon;
        bvm2::get_CidViaSid(cidCocoon, sidCocoon, Blob(nullptr, 0));
        std::string sCidCocoon = cidCocoon.str();


        printf("scheduling upgrade...\n");

        Height hTarget = node.get_Processor().m_Cursor.m_Full.m_Height + 20;

        for (uint32_t i = 0; i < nPeers; i++)
        {
            pMan[i]->m_Args["action"] = "schedule_upgrade";
            pMan[i]->m_Args["cidVersion"] = sCidCocoon;
            pMan[i]->m_Args["hTarget"] = std::to_string(hTarget);

            if (iSender == i)
            {
                pMan[i]->RunSync0(1);
                WALLET_CHECK(!pMan[i]->m_Done);
            }
        }

        for (uint32_t i = 0; i < nPeers; i++)
        {
            if (iSender != i)
            {
                pMan[i]->RunSync0(1);
                WALLET_CHECK(!pMan[i]->m_Done);
            }
        }

        pMan[iSender]->RunSync1(); // wait synchronously

        WALLET_CHECK(pMan[iSender]->m_Done && !pMan[iSender]->m_Err);
        WALLET_CHECK(pMan[iSender]->DoTx(completedCount));

        printf("checking state...\n");

        pMan[iSender]->m_Args["action"] = "view";
        pMan[iSender]->RunSync(1);
        WALLET_CHECK(pMan[iSender]->m_Done && !pMan[iSender]->m_Err);

        // scheduled version would be unrecognizable, never mind this.


        printf("waiting for upgrade height...\n");

        if (node.get_Processor().m_Cursor.m_Full.m_Height < hTarget)
        {
            struct MyObserver :public Node::IObserver
            {
                Node& m_Node;
                Height m_hTarget;

                MyObserver(Node& n) :m_Node(n)
                {
                    m_Node.m_Cfg.m_Observer = this;
                }

                ~MyObserver()
                {
                    m_Node.m_Cfg.m_Observer = nullptr;
                }

                void OnSyncProgress() override {}

                void OnStateChanged() override {
                    if (m_Node.get_Processor().m_Cursor.m_Full.m_Height >= m_hTarget)
                        io::Reactor::get_Current().stop();
                }

            };

            MyObserver obs(node);
            obs.m_hTarget = hTarget;
            io::Reactor::get_Current().run();
        }

        printf("triggering upgrade...\n");

        pMan[iSender]->m_Args["action"] = "explicit_upgrade";

        pMan[iSender]->RunSync(1);
        WALLET_CHECK(pMan[iSender]->m_Done && !pMan[iSender]->m_Err);

        WALLET_CHECK(pMan[iSender]->DoTx(completedCount));


        printf("checking status...\n");

        MyManager::Compile(pMan[iSender]->m_BodyManager, "upgradable3/Test/test_app.wasm", MyManager::Kind::Manager);

        pMan[iSender]->m_Args["action"] = "view";
        pMan[iSender]->RunSync(1);
        WALLET_CHECK(pMan[iSender]->m_Done && !pMan[iSender]->m_Err);

        WALLET_CHECK(MyManager::get_OutpStrEx(pMan[iSender]->m_Out.str(), "\"contracts\":", sTmp));
        std::string sVer;
        WALLET_CHECK(MyManager::get_OutpStrEx(sTmp, "\"version\": ", sVer, 1));
        WALLET_CHECK(sVer == "0");
    }


    void TestAppShader3()
    {
        printf("Testing upgradable3...\n");

        io::Reactor::Ptr mainReactor(io::Reactor::create());
        io::Reactor::Scope scope(*mainReactor);

        string nodePath = "node.db";
        if (boost::filesystem::exists(nodePath))
            boost::filesystem::remove(nodePath);

        int completedCount = 0;

        //auto timer = io::Timer::create(io::Reactor::get_Current());
        auto f = [&completedCount, mainReactor](auto txID)
        {
            --completedCount;
            if (completedCount == 0)
            {
              //  timer->cancel();
                mainReactor->stop();
            }
        };

        constexpr uint32_t nPeers = 5;
        constexpr uint32_t iSender = 3;
        wallet::IWalletDB::Ptr pDb[nPeers];
        for (uint32_t i = 0; i < nPeers; i++)
        {
            std::string sPath = std::string("sender_") + std::to_string(i) + "wallet.db";
            pDb[i] = createSqliteWalletDB(sPath, false, true);
        }

        auto treasury = createTreasury(pDb[iSender], AmountList{Rules::Coin * 300000});

        auto nodeCreator = [](Node& node, const ByteBuffer& treasury, uint16_t port, const std::string& path, const std::vector<io::Address>& peers = {}, bool miningNode = true)->io::Address
        {
            InitNodeToTest(node, treasury, nullptr, port, 3000, path, peers, miningNode);
            io::Address address;
            address.resolve("127.0.0.1");
            address.port(port);
            return address;
        };


        Node node;
        auto nodeAddress = nodeCreator(node, treasury, 32125, "node.db");

        std::unique_ptr<TestWalletRig> pRig[nPeers];
        std::unique_ptr<MyManager> pMan[nPeers];

        for (uint32_t i = 0; i < nPeers; i++)
        {
            pRig[i] = std::make_unique<TestWalletRig>(pDb[i], f, TestWalletRig::Type::Regular, false, 0, nodeAddress);
            pMan[i] = std::make_unique<MyManager>(pDb[i], pRig[i]->m_Wallet);
            pMan[i]->set_Privilege(2);

            if (i)
                pMan[i]->m_BodyManager = pMan[0]->m_BodyManager;
            else
                MyManager::Compile(pMan[i]->m_BodyManager, "upgradable3/Test/test_app.wasm", MyManager::Kind::Manager);
        }

        printf("collecting keys...\n");

        std::string pAdminKey[nPeers];
        for (uint32_t i = 0; i < nPeers; i++)
        {
            pMan[i]->m_Args["role"] = "manager";
            pMan[i]->m_Args["action"] = "my_admin_key";
            pMan[i]->RunSync(1);
            WALLET_CHECK(pMan[i]->m_Done && !pMan[i]->m_Err);

            WALLET_CHECK(pMan[i]->get_OutpStr("\"admin_key\": \"", pAdminKey[i], sizeof(ECC::Point) * 2));
        }


        printf("Deploying v0\n");

        MyManager::Compile(pMan[iSender]->m_BodyContract, "upgradable3/Test/test_v0.wasm", MyManager::Kind::Contract);
        pMan[iSender]->m_Args["role"] = "manager";
        pMan[iSender]->m_Args["action"] = "deploy";
        pMan[iSender]->m_Args["hUpgradeDelay"] = "1";
        pMan[iSender]->m_Args["nMinApprovers"] = std::to_string(nPeers);

        for (uint32_t i = 0; i < nPeers; i++)
            pMan[iSender]->m_Args[std::string("admin_") + std::to_string(i)] = pAdminKey[i];

        pMan[iSender]->RunSync(1);
        WALLET_CHECK(pMan[iSender]->m_Done && !pMan[iSender]->m_Err);

        WALLET_CHECK(pMan[iSender]->DoTx(completedCount));


        printf("reading cid and version...\n");

        pMan[iSender]->m_Args["action"] = "view";
        pMan[iSender]->RunSync(1);
        WALLET_CHECK(pMan[iSender]->m_Done && !pMan[iSender]->m_Err);

        std::string sTmp, sCid, sVer;
        WALLET_CHECK(MyManager::get_OutpStrEx(pMan[iSender]->m_Out.str(), "\"contracts\":", sTmp));
        WALLET_CHECK(MyManager::get_OutpStrEx(sTmp, "\"cid\": \"", sCid, bvm2::ContractID::nTxtLen));
        WALLET_CHECK(MyManager::get_OutpStrEx(sTmp, "\"version\": ", sVer, 1));
        WALLET_CHECK(sVer == "0");

        printf("scheduling upgrade...\n");

        MyManager::Compile(pMan[iSender]->m_BodyContract, "upgradable3/Test/test_v1.wasm", MyManager::Kind::Contract);

        Height hTarget = node.get_Processor().m_Cursor.m_Full.m_Height + 15;

        for (uint32_t i = 0; i < nPeers; i++)
        {
            pMan[i]->m_Args["action"] = "schedule_upgrade";
            pMan[i]->m_Args["cid"] = sCid;
            pMan[i]->m_Args["hTarget"] = std::to_string(hTarget);
            pMan[i]->m_Args["iSender"] = std::to_string(iSender);
            pMan[i]->m_Args["approve_mask"] = std::to_string((1U << nPeers) - 1);

            if (iSender == i)
            {
                pMan[i]->RunSync0(1);
                WALLET_CHECK(!pMan[i]->m_Done);
            }
        }

        for (uint32_t i = 0; i < nPeers; i++)
        {
            if (iSender != i)
            {
                pMan[i]->m_BodyContract = pMan[iSender]->m_BodyContract;
                pMan[i]->RunSync0(1);
                WALLET_CHECK(!pMan[i]->m_Done);
            }
        }

        pMan[iSender]->RunSync1(); // wait synchronously

        WALLET_CHECK(pMan[iSender]->m_Done && !pMan[iSender]->m_Err);
        WALLET_CHECK(pMan[iSender]->DoTx(completedCount));

        printf("checking state...\n");

        pMan[iSender]->m_Args["action"] = "view";
        pMan[iSender]->RunSync(1);
        WALLET_CHECK(pMan[iSender]->m_Done && !pMan[iSender]->m_Err);

        WALLET_CHECK(MyManager::get_OutpStrEx(pMan[iSender]->m_Out.str(), "\"contracts\":", sTmp));
        WALLET_CHECK(MyManager::get_OutpStrEx(sTmp, "\"version\": ", sVer, 1));
        WALLET_CHECK(sVer == "0");

        printf("waiting for upgrade height...\n");

        if (node.get_Processor().m_Cursor.m_Full.m_Height < hTarget)
        {
            struct MyObserver :public Node::IObserver
            {
                Node& m_Node;
                Height m_hTarget;

                MyObserver(Node& n) :m_Node(n)
                {
                    m_Node.m_Cfg.m_Observer = this;
                }

                ~MyObserver()
                {
                    m_Node.m_Cfg.m_Observer = nullptr;
                }

                void OnSyncProgress() override {}

                void OnStateChanged() override {
                    if (m_Node.get_Processor().m_Cursor.m_Full.m_Height >= m_hTarget)
                        io::Reactor::get_Current().stop();
                }

            };

            MyObserver obs(node);
            obs.m_hTarget = hTarget;
            io::Reactor::get_Current().run();
        }

        printf("upgrading...\n");

        pMan[iSender]->m_Args["action"] = "explicit_upgrade";
        pMan[iSender]->RunSync(1);
        WALLET_CHECK(pMan[iSender]->m_Done && !pMan[iSender]->m_Err);
        WALLET_CHECK(pMan[iSender]->DoTx(completedCount));

        printf("checking state...\n");

        pMan[iSender]->m_Args["action"] = "view";
        pMan[iSender]->RunSync(1);
        WALLET_CHECK(pMan[iSender]->m_Done && !pMan[iSender]->m_Err);

        WALLET_CHECK(MyManager::get_OutpStrEx(pMan[iSender]->m_Out.str(), "\"contracts\":", sTmp));
        WALLET_CHECK(MyManager::get_OutpStrEx(sTmp, "\"version\": ", sVer, 1));
        WALLET_CHECK(sVer == "1");
    }


    void TestAppShader4()
    {
        printf("Testing dao-vault...\n");

        io::Reactor::Ptr mainReactor(io::Reactor::create());
        io::Reactor::Scope scope(*mainReactor);

        string nodePath = "node.db";
        if (boost::filesystem::exists(nodePath))
            boost::filesystem::remove(nodePath);

        int completedCount = 0;

        //auto timer = io::Timer::create(io::Reactor::get_Current());
        auto f = [&completedCount, mainReactor](auto txID)
        {
            --completedCount;
            if (completedCount == 0)
            {
              //  timer->cancel();
                mainReactor->stop();
            }
        };

        constexpr uint32_t nPeers = 5;
        constexpr uint32_t iSender = 3;
        wallet::IWalletDB::Ptr pDb[nPeers];
        for (uint32_t i = 0; i < nPeers; i++)
        {
            std::string sPath = std::string("sender_") + std::to_string(i) + "wallet.db";
            pDb[i] = createSqliteWalletDB(sPath, false, true);
        }

        auto treasury = createTreasury(pDb[iSender], AmountList{Rules::Coin * 300000});

        auto nodeCreator = [](Node& node, const ByteBuffer& treasury, uint16_t port, const std::string& path, const std::vector<io::Address>& peers = {}, bool miningNode = true)->io::Address
        {
            InitNodeToTest(node, treasury, nullptr, port, 3000, path, peers, miningNode);
            io::Address address;
            address.resolve("127.0.0.1");
            address.port(port);
            return address;
        };


        Node node;
        auto nodeAddress = nodeCreator(node, treasury, 32125, "node.db");

        std::unique_ptr<TestWalletRig> pRig[nPeers];
        std::unique_ptr<MyManager> pMan[nPeers];

        for (uint32_t i = 0; i < nPeers; i++)
        {
            pRig[i] = std::make_unique<TestWalletRig>(pDb[i], f, TestWalletRig::Type::Regular, false, 0, nodeAddress);
            pMan[i] = std::make_unique<MyManager>(pDb[i], pRig[i]->m_Wallet);
            pMan[i]->set_Privilege(2);

            if (i)
                pMan[i]->m_BodyManager = pMan[0]->m_BodyManager;
            else
                MyManager::Compile(pMan[i]->m_BodyManager, "dao-vault/app.wasm", MyManager::Kind::Manager);
        }

        printf("collecting keys...\n");

        std::string pAdminKey[nPeers];
        for (uint32_t i = 0; i < nPeers; i++)
        {
            pMan[i]->m_Args["role"] = "manager";
            pMan[i]->m_Args["action"] = "my_admin_key";
            pMan[i]->RunSync(1);
            WALLET_CHECK(pMan[i]->m_Done && !pMan[i]->m_Err);

            WALLET_CHECK(pMan[i]->get_OutpStr("\"admin_key\": \"", pAdminKey[i], sizeof(ECC::Point) * 2));
        }


        printf("Deploying dao-vault\n");

        MyManager::Compile(pMan[iSender]->m_BodyContract, "dao-vault/contract.wasm", MyManager::Kind::Contract);
        pMan[iSender]->m_Args["role"] = "manager";
        pMan[iSender]->m_Args["action"] = "deploy";
        pMan[iSender]->m_Args["hUpgradeDelay"] = "1";
        pMan[iSender]->m_Args["nMinApprovers"] = std::to_string(nPeers);

        for (uint32_t i = 0; i < nPeers; i++)
            pMan[iSender]->m_Args[std::string("admin_") + std::to_string(i)] = pAdminKey[i];

        pMan[iSender]->RunSync(1);
        WALLET_CHECK(pMan[iSender]->m_Done && !pMan[iSender]->m_Err);

        WALLET_CHECK(pMan[iSender]->DoTx(completedCount));


        printf("reading cid...\n");

        pMan[iSender]->m_Args["action"] = "view";
        pMan[iSender]->RunSync(1);
        WALLET_CHECK(pMan[iSender]->m_Done && !pMan[iSender]->m_Err);

        std::string sTmp, sCid;
        WALLET_CHECK(MyManager::get_OutpStrEx(pMan[iSender]->m_Out.str(), "\"contracts\":", sTmp));
        WALLET_CHECK(MyManager::get_OutpStrEx(sTmp, "\"cid\": \"", sCid, bvm2::ContractID::nTxtLen));

        const Amount amount = Rules::Coin * 100;


        printf("depositing...\n");

        pMan[iSender]->m_Args["cid"] = sCid;
        pMan[iSender]->m_Args["action"] = "deposit";
        pMan[iSender]->m_Args["amount"] = std::to_string(amount);

        pMan[iSender]->RunSync(1);

        WALLET_CHECK(pMan[iSender]->m_Done && !pMan[iSender]->m_Err);
        WALLET_CHECK(pMan[iSender]->DoTx(completedCount));


        printf("withdrawing...\n");

        for (uint32_t i = 0; i < nPeers; i++)
        {
            pMan[i]->m_Args["cid"] = sCid;
            pMan[i]->m_Args["action"] = "withdraw";
            pMan[i]->m_Args["amount"] = std::to_string(amount);
            pMan[i]->m_Args["iSender"] = std::to_string(iSender);
            pMan[i]->m_Args["approve_mask"] = std::to_string((1U << nPeers) - 1);

            if (iSender == i)
            {
                pMan[i]->RunSync0(1);
                WALLET_CHECK(!pMan[i]->m_Done);
            }
        }

        for (uint32_t i = 0; i < nPeers; i++)
        {
            if (iSender != i)
            {
                pMan[i]->m_BodyContract = pMan[iSender]->m_BodyContract;
                pMan[i]->RunSync0(1);
                WALLET_CHECK(!pMan[i]->m_Done);
            }
        }

        pMan[iSender]->RunSync1(); // wait synchronously

        WALLET_CHECK(pMan[iSender]->m_Done && !pMan[iSender]->m_Err);
        WALLET_CHECK(pMan[iSender]->DoTx(completedCount));
    }

    struct MyZeroInit {
        static void Do(std::string&) {}
        static void Do(beam::ShieldedTxo::PublicGen& pg)
        {
            SecString secretSeed;
            //secretSeed.assign(buf.data(), buf.size());

            Key::IKdf::Ptr kdf;
            ECC::HKdf::Create(kdf, secretSeed.hash().V);
            pg = GeneratePublicAddress(*kdf);
        }
        template <typename T> static void Do(std::vector<T>&) {}
        // fill with valid wallet id
        static void Do(WalletID& id) { id.FromHex("1b516fb39884a3281bc0761f97817782a8bc51fdb1336882a2c7efebdb400d00d4"); } 
        template <typename T> static void Do(T& x) { ZeroObject(x);  }
    };

    void TestTxParameters()
    {
        std::cout << "Testing tx parameters and token...\n";

        {

            WalletID myID(Zero);
            WALLET_CHECK(myID.FromHex("7a3b9afd0f6bba147a4e044329b135424ca3a57ab9982fe68747010a71e0cac3f3"));
            {
                WalletID peerID(Zero);
                WALLET_CHECK(peerID.FromHex("6fb39884a3281bc0761f97817782a8bc51fdb1336882a2c7efebdb400d00d4"));
                // WALLET_CHECK(peerID.IsValid());
            }

            WalletID peerID(Zero);
            WALLET_CHECK(peerID.FromHex("1b516fb39884a3281bc0761f97817782a8bc51fdb1336882a2c7efebdb400d00d4"));
            auto params = CreateSimpleTransactionParameters()
                .SetParameter(TxParameterID::MyAddr, myID)
                .SetParameter(TxParameterID::PeerAddr, peerID)
                .SetParameter(TxParameterID::Lifetime, Height(200));


            string token = to_string(params);

            auto restoredParams = wallet::ParseParameters(token);

            WALLET_CHECK(restoredParams && *restoredParams == params);

            string address = to_string(myID);
            auto addrParams = wallet::ParseParameters(address);
            WALLET_CHECK(addrParams && *addrParams->GetParameter<WalletID>(TxParameterID::PeerAddr) == myID);

            const string addresses[] =
            {
                "7a3b9afd0f6bba147a4e044329b135424ca3a57ab9982fe68747010a71e0cac3f3",
                "9f03ab404a243fd09f827e8941e419e523a5b21e17c70563bfbc211dbe0e87ca95",
                "0103ab404a243fd09f827e8941e419e523a5b21e17c70563bfbc211dbe0e87ca95"
            };
            for (const auto& a : addresses)
            {
                WalletID id(Zero);
                WALLET_CHECK(id.FromHex(a));
                boost::optional<TxParameters> p;
                WALLET_CHECK_NO_THROW(p = wallet::ParseParameters(a));
                WALLET_CHECK(p && *p->GetParameter<WalletID>(TxParameterID::PeerAddr) == id);
            }


            {
                auto jsParams = CreateSimpleTransactionParameters()
                    .SetParameter(TxParameterID::MyAddr, myID)
                    .SetParameter(TxParameterID::PeerAddr, peerID)
                    .SetParameter(TxParameterID::Lifetime, Height(200))
                    .SetParameter(TxParameterID::Amount, Amount(234))
                    .SetParameter(TxParameterID::AmountList, AmountList{Amount(2), Amount(34)})
                    .SetParameter(TxParameterID::Fee, Amount(32));

                auto token1 = std::to_string(jsParams);
                auto jsonParams = ConvertTokenToJson(token1);
                auto token2 = ConvertJsonToToken(jsonParams);
                auto jsonParams2 = ConvertTokenToJson(token2);
                WALLET_CHECK(jsonParams == jsonParams2);
                WALLET_CHECK(token1 == token2);
            }
            {
                std::string addr = "vn9dekHru14SuuU7otEzdVztGDAaKT6ku7HigdhydPfzQry3rxpVXvbAnmCeBVdCR7rudexjueDVz863cyM2CQtmtKfxuVZNHotgyp8SzNM9NyHKTB8h8MiwuWgLpnhzVEznYFnqZTestf6kQMUfiEsMpMCTv3EhQRyjjx22cbZnmih43hwapEkfiWetM8UjvToQ1ZWu8EMuNGGWKyBHPxeCPvJvJRiDYjZMqyxcAccYvvj6p6A2iEnEEBs7V2sxQLLDtobP7Jmoiv5kRJUxjf3Lmda9dUin7YqXRy8tei1BDPakW7tC3X69BAg4S6ocSSLLArjHiSVF4Y4f9q3k54nEcHCc41wTZpLkh8kjQB6Rv12gCuRxELUgno6PGfMQaSjkzxCorVWRnRqR7jbepyxiikNaJGzKQugoysFKSRjfWfTzgyzHKcorJmZ4qEueE5fncWgMEkLGmobxVwpNzy3";
                WALLET_CHECK(CheckReceiverAddress(addr));
                auto p = ConvertTokenToJson(addr);
                WALLET_CHECK(!p.empty());
            }
            {
                auto p = ConvertTokenToJson("9DfkirZYqjTSuHBGYKxb3ifi8QPEM6xcQewGQVuyKgfMB4cGAfsZLaXNSSMk9DULVwdiQu8mcATtxZcwk6hnb27EJgmhCueJErQeLcfFdvF4n72dbcy5H7MNfr67fukF1bdMt2sNV93seDPZikFsNPxub7XbWumW5pa5Ce93oyFZ4WACeiFid3GbMdkC5418MNzhhWujqjExPmhxvK7YxUZeHQKJ71583mia5rbSTXYZBMHCmkoP2wYjpakt2g7dHMgLVUbpj6MRxsyGNBEecBL8889HVxVxpzyy44BvbxeVD6mdxHWVmntNhjEYnUfnvF65bkNgzSwpTMuKaiDQiHyX7oBJexUHkwVdUsSQcXHrAfaeKZx14e2BcZv9CfG8HB37zA93Gxb3atux6XXQXU8uyJanr9G66fCbw2LFzCPBKMeDanPU21ts25KMMtb6V5JebcLiJfeiMqbwmJ");
                json obj = json::parse(p);
                
                auto t = obj["params"]["PeerID"].get<std::string>();
                WALLET_CHECK(t == "3c59d8d635a3971b7c55b26010a280921356f007329770a5e6955d83740b6ed6de1");
            }
            {
                // don't save uninitialized variables
                TxParameters allParams;
#define MACRO(name, index, type) { \
                    type var; \
                    MyZeroInit::Do(var); \
                    allParams.SetParameter(TxParameterID::name, var); \
                }

                BEAM_TX_PUBLIC_PARAMETERS_MAP(MACRO)
#undef MACRO

                allParams.DeleteParameter(TxParameterID::SubTxIndex);
                allParams.DeleteParameter(TxParameterID::PublicAddreessGen);
                auto token1 = std::to_string(allParams);

                auto jsonParams = ConvertTokenToJson(token1);
                WALLET_CHECK(!jsonParams.empty());
                auto token2 = ConvertJsonToToken(jsonParams);
                auto jsonParams2 = ConvertTokenToJson(token2);
                WALLET_CHECK(jsonParams == jsonParams2);
                //WALLET_CHECK(token1 == token2);
            }
        }
        {
            std::string s = "3ab404a243fd09f827e8941e419e523a5b21e17c70563bfbc211dbe0e87ca95";
            auto identity = GetPeerIDFromHex(s);
            WALLET_CHECK(identity.is_initialized());
            auto r = std::to_string(*identity);
            WALLET_CHECK(r == s);
        }

        {

            std::string amount = to_base64(Amount(0));
            WALLET_CHECK(from_base64<beam::Amount>(amount) == 0);
            WALLET_CHECK(from_base64<beam::Amount>(to_base64(Amount(100))) == 100);
        }

        {
            std::string sbbsAddressStr = "7a3b9afd0f6bba147a4e044329b135424ca3a57ab9982fe68747010a71e0cac3f3";
            std::string endpointStr = "3ab404a243fd09f827e8941e419e523a5b21e17c70563bfbc211dbe0e87ca95";
            auto token = GetSendToken(sbbsAddressStr, endpointStr, Amount(11));
            auto p = wallet::ParseParameters(token);
            WALLET_CHECK(p.is_initialized());
            const TxParameters& p2 = *p;
            WalletID address;
            WALLET_CHECK(address.FromHex(sbbsAddressStr));
            WALLET_CHECK(*p2.GetParameter<WalletID>(TxParameterID::PeerAddr) == address);
            auto  endpoint = GetPeerIDFromHex(endpointStr);
            WALLET_CHECK(endpoint.is_initialized());
            WALLET_CHECK(*endpoint == *p2.GetParameter<PeerID>(TxParameterID::PeerEndpoint));
            WALLET_CHECK(*p2.GetParameter<Amount>(TxParameterID::Amount) == Amount(11));
        }
        { // invalid channel
            std::string sbbsAddressStr = "b0ca7b4afd7f0000fe6d24e8fd052ef04ff4bb2a230a81c8eeeb0dd0e55af766a91c6513e377fb39";
            WALLET_CHECK(beam::wallet::CheckReceiverAddress(sbbsAddressStr) == false);
        }
        {
            WALLET_CHECK(!CheckReceiverAddress("6dFBAa1SQ6gtPdZGimrFpxr6ByuQSg3XyzXAjXb5xTrj7x1izcFv29ropCqXs5opBUCN9uS4fCJpR2HEUYhmfpRvTijFcVsP"));
        }

        {
            WalletID invalidWalletID;
            WALLET_CHECK(invalidWalletID.FromHex("1b516fb37884a3281bc07610000008bc51fdb1336882a2c7efebdb400d00d4"));
            WALLET_CHECK(!invalidWalletID.IsValid());
            TxParameters p;
            p.SetParameter(TxParameterID::PeerAddr, invalidWalletID);
            TxToken invalidToken(p);
            WALLET_CHECK(!invalidToken.IsValid());
            WALLET_CHECK(!CheckReceiverAddress(std::to_string(p)));
            WALLET_CHECK(!CheckReceiverAddress("6dFBAa1SQ6gtPdZGimrFpxr6ByuQSg3XyzXAjXb5xTrj7x1izcFv29ropCqXs5opBUCN9uS4fCJpR2HEUYhmfpRvTijFcVsP"));
        }
        {
            WalletID validWalletID;
            WALLET_CHECK(validWalletID.FromHex("7a3b9afd0f6bba147a4e044329b135424ca3a57ab9982fe68747010a71e0cac3f3"));
            WALLET_CHECK(validWalletID.IsValid());
            TxParameters p;
            p.SetParameter(TxParameterID::PeerAddr, validWalletID);
            TxToken invalidToken(p);
            WALLET_CHECK(invalidToken.IsValid());
            WALLET_CHECK(CheckReceiverAddress(std::to_string(p)));
        }

        {
            WALLET_CHECK(GetAddressType("2d37efaaf8550a576d3dabb9bb2799dfe1b40b5bd29c6436ccd729744c806a7355e") == TxAddressType::Unknown);
            WALLET_CHECK(GetAddressType("VnodAMahC2nTGmLYDqLpuPzziGhg9veyJ1BC6W1CrCd5RRcwxduMUmLJpFAekC1uJYqrYDRrouT4wXNDVqH6QdHggAETr4HF6fAoubsV8YX92WvRZ4Ga6BftT4GfRaNmjk") == TxAddressType::Unknown);
            WALLET_CHECK(GetAddressType("2d37efaaf8e80a576d3dabb9bb2799dfe1b40b5bd29c6436ccd729744c806a7355e") == TxAddressType::Regular);
            WALLET_CHECK(GetAddressType("VnodAMahC2nTGmLYDqLpuPzziGhg9veyJ1BC6W1CrCd5RRcwxduMUmLJpFAe2YtZsUZ2BRyZQdNgfXkU6jNH6yAy7yMNnjiafvXnd5b142MqnEEEzkC1uJYqrYDRrouT4wXNDVqH6QdHggAETr4HF6fAoubsV8YX92WvRZ4Ga6BftT4GfRaNmjk") == TxAddressType::Regular);
            WALLET_CHECK(GetAddressType("PMaqLZQ3WZK2RUkyoYmasoXFcQo6T3WmoF1qAhQDQSJaQMpft6RjHSesbWKrc885CgM7DoJemPAm9vYHkNhQx82mZqwKJF5ELiB5JHaCvqLwbHGRzkak8RC4ia99ZPZ4Q9gkSDh5oumUoQ41kk79HDRoF8PAcdwxR1L1XsBShNYTZE12RZZ5R7LsVTZder8GchoFPRqZk895pFazLjyzk4ps9dYn5FcG43mVd6A5grbjMoMCT2M4j1xti9Qzc2ni85h5f6mpsXQwxYpzu9jkDyzMY6ZuNobw34j1yMzygyrc8K9dguBvy491MWGDvY9g99XBJHkDUQcVPLWvNemMPEgcSFzYMi6nhJ4MZ8Dzhfr7JYz8jVTBkUsyKdvC3uyEPjEzkqDpSjTVqWsCkvBy3Tfafm9DoVpuXtFJacf7YvQwgSPfGB4EcqaSwD2ccLw8") == TxAddressType::MaxPrivacy);
            WALLET_CHECK(GetAddressType("51su6ZRjAm2AFwNSXJKyZJAeCwR12oRUoyTcAutF9wF7NUvvecdM7Abk73UsmhEPChLioPxE2AvFi1UqL47VWEAxvaBzwqpyCTemF3dFgzWQoUqxrtUR6yKXPGA17TeZTsrqWYYACBZi3RNSQeXyfbxGCiCjeEa5ozCFVaBXMKL4Uj7h6RJg7Y8g31k4JFNiE5aDEmGjr4sC9DMXLJm19S2zTdGVFGAA4SAXJzeddwEj2aoepJnToirdvsjVyKbrL8NyfGWAdk2xsLk5XQRcsF6aBsCkMcUnWXThZ5mmo5SHr56K9VXMw6E5jGtzdxB5jdbuRurgF1HzaquXzQ4TVUbWRxCd2NfLghtJSxCxcqnHyktYjFtv924cMX8ssrHFnmvsUKifV7C3q9b9iBcXh935V5kiJK5jXBGxLz96UVB1ppgKXTzUZ9Kke5RFXaprPuNmN2SsDQTvojKVjWQWjNAMeURxPjqxGzFnggM6JRPzGCgCEq3J6wa8rZwcVgTypGRxSBELpY9c8F1EtRsRyA4fhj5KBk5EHdNMEL6bjwMaEWfrPfhBxAbPrQr23zRg79FHCiWhcQJNVNW8YBF3iKZH5XQzFEsbxRMhK9aajgsu1eE8Sag6ke9zHqvoFMwuzzmXVJDW93UHbYLx9S6kBoM2xBg5zvdJhPNjhyhG14fjPcwaAaqBADGPRi1uU6oL8Fw64bQLyUi3iML11iKXF8H7igfELGtNLcFf9jq3GnWRH49Q8YACXXmpcXRAR6V8cBp1orGY1Hx1zHtnezu4duX7UhUz3XLc7RWESwtNyDupuJURVhB6F4v936VXPTjuh8e56y9dZghF2Nf2CUm9fvn8Gxi2tgnpEz4F4tT5g5S9yWznMKotqYs6kcJ1VURRDeNw5vqWxCPaRoiH7or5pzQ8dpMxsit9jGdQxftFyXhY2vFjyWHKy5fj6hd1eLDP7d5rxsFsJNmKt3aRW1nk3493GDLhcQLjbxBDCXE3s1Xy6iCsUMd4hK8bRyztgGszoN1n3MgKvbjy8jUKd4aP3xHUrMkVc9Lpm7KXtqqp7BwuhXThbU71wqr9LmBPpiDMQz242KQPGsgatpyvsWqepVg818NmAAb2v3a5n969QG9nmcB6Z9ZporQcD5sYrieQP7Lc634DfjLAKRXfSEVoFiPsigMrH6V99fkBeU1HEvK3BBkJuzaCqxJWTtasQWUF8dahaJSYtvLBfzTHFrojFQ9qzxoVbkd9nmS4co8HDAr5yAd9KZ2LLWChs1AeC5QKukD3j7GsqaT2Vgsm2H8grPYfCa7LGiqNrUxUb32Fmda5PQPtMUZcvwEnNoopy6HjB3XKsUzK4d4WFMqkru9Vc9W1wbWCRyEEiuYd5dZwADriDuQJmJweBTdMVNAL9bzin4AcJyyEQRxiibvW1ZK3Fihc3HPSNWBhLEFoJcPSUrJ8kXv1jKckyJY2aLL9zN5qq3HxP5Un4FJigLEu45RMZaog3nDG6apTjKh6MiQB3zzrMocRyaaRDRPFdDrYjxQmy92dgrwGiRmhf8ughkyenuNCCuXrnB1zcRi4AWf7hayWVtXSYadzFniz8iztrfcrNEnsKA8Zf2uc1YreA6YyuVNNEzHRhavqiTfhy8kzw7FzPrhYunrxcSnucJJnSR7CongADmLHrAMp9cG9CsA7xVSXosLhbzUdq2Q3cKzihNUm9EEmjJFJ2BsSXnnWCvx7zixpZtRK6crddr4bpayww83hY5uusHmjTQx1oEi19UQNceXCQnCQHxmTQ1kPJsA4GHMAYsaRFob4VfJRYiuKdVkGGHUJKfaL8vuieyEQj7Mw1rAssabkqgec4Mzbpf98ATXWRhcDrR1hQCS3qB7oNGfZ3zn1SjzUHid6AjwpGYL5fhfd5BibnC6S5erxuFULidDKqt75zKcHCtVzu8u2QfJVtr2beJYUgcz4WdaY5Z5r8SKt3md89J5BKep1QLEx38WSFFYibNVmZzqGMa3nGJQgHS5VsJLjQtDVKzP9pt1er5b2rvJvohKB2zAfTg5fjR5ohJ76Ey5m4WnMR8RL4mfZvUAkMQraWzo5xtE3QCrfb25hwocjKH88hhCzDF7fAGVmq38ZW3T6aqAEt55ZfJDgtiadniofLcboLazkEQYvXTc9HcWTc7S6ckoKmUxXH1ovqbEBvBq5VM7cb2vmi1BcycoDjwkNZvuxQDZj6GcYjQyrq7K21LL6HZvVe2y1j7prPJtUu1vb43F6A2Nxg3aV6FfUtbMZBYMt3myyYAfowyQoEdE8NiL8riGryaAEKcHR7EbshSg3RwHMuUbvMLtYiiMbW4K1FK8XJG5SHey88p9RGENqx6VxqccbBUoqZxHvoNTMX2SvkVrUATdyzXPJUxAo3qDf6D8rbzpMh7MgbXJ3TG47UiGwevDQWtikZTWsgkVDV69xSxu2dq5ANLddZxY8Dsm4JW7F8RrDMzUC6CbnuDc4StnLdknY4A3um7XBc7YQZRwjavjaVxotWboq5nrrjDjMTmZfxbcW4PS7J1W1uHprtdZxwnXq3pCE5EE4heCzcMCAfrhvnUxdGqmaeYnF4aEE1WooZnTtVhUAyWTQXMSREoLcdAoeJ2hFXibeFYqFrBf2QAW5DtH5oQmgA2vx3oCJbEqK9xVvTqoGwZqejcQrHQZtWCnXSbtZSVia2epV2L2ccszgTZhNEr8oZJ1CZCd14VH1Wu9JmtwQWMWsVkjUxzbUy9MHjnL5J6rEvnnEtLf8KM4LdJcEbbb6My76iyYv2qCZYh5rLYkzUWR8A7UQp5TZuZNBqTFXvhoy8cnyv9vtyTQS59YABHejixaZtJXvSrE2ZziuxLJiiG37KYaRvc6gQapyCG2MZXYvsdxZGyk8KcboEQwVS3agTab9AEW6kMxgVvzH4Ee2bJhswQHL9SWLsH6kZFCp59kZtA8RLE3esJx9k7PUp1dXSB3KzxeMCdmfmcAF6rJhwgT9jz7RxivhcGdqVKnEqTdWaSr23vHhj6YYggxkUUEjN1ZXT4FdvrZsMGtfsirb5wyS4q5KDTAkYF8rykrzTzUwHp7LBagKneFQM8Jh4U4EJqRMS5HAeViBwKHXj7cTTtGzL82iv8NeycHKJYExVWLg5JWCnzatekJUDwoDbTawa1ACXLYVwMxNUteQZ83Ybn3o1ZeeKtUn3ZwRjhRQDr8AjmUw1UNANXWbCgL8ibjLnD285nKKJ") == TxAddressType::Offline);
            WALLET_CHECK(GetAddressType("344t9xwfSpreKLKTJHG1PMiD9B3zgXu7hjnwo479wdqZGFVHvbLc1jsGcNZw6j6Xu8WsoMvDV1QpPtcoXqpPMgbmyYvxojihjrzYm566rEFKxzuW7tFYaQ4i8TajabgRfQS32QCAYCDWuAqNHXYYePT5CkvkwiVSUaBchqNkQh7zAmLuPJrRy1WjTDY3d6obefygYvm4yz8aBoZ8XFgqGuFn8omz6iD14adyBUkzEtL71VmS4HeYwHd1CDvwLGnz44wYo6A3TRRSEJAPTRo2YNjDodan4FAfKzBcMkzBQ") == TxAddressType::PublicOffline);
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
  
    struct TestWalletClient : public WalletClient
    {
        TestWalletClient(IWalletDB::Ptr walletDB, const std::string& nodeAddr, io::Reactor::Ptr reactor)
            : WalletClient(Rules::get(), walletDB, nodeAddr, reactor)
        {
        }

        virtual ~TestWalletClient()
        {
            stopReactor();
        }
    };

    void TestClient()
    {
        std::cout << "Testing wallet client...\n";
        io::Reactor::Ptr mainReactor(io::Reactor::create());
        io::Reactor::Scope scope(*mainReactor);
        auto db = createSenderWalletDB();
        std::string nodeAddr = "127.0.0.1:32546";
        TestWalletClient client(db, nodeAddr, io::Reactor::create());
        {
            std::map<Notification::Type,bool> activeNotifications {
                { Notification::Type::SoftwareUpdateAvailable, true },
                { Notification::Type::AddressStatusChanged, true },
                { Notification::Type::WalletImplUpdateAvailable, true },
                { Notification::Type::BeamNews, true },
                { Notification::Type::TransactionFailed, true },
                { Notification::Type::TransactionCompleted, true }
            };
            client.start(activeNotifications);
        }
        auto timer = io::Timer::create(*mainReactor);
        
        auto startEvent = io::AsyncEvent::create(*mainReactor, [&timer, mainReactor, &client, db]()
        {
            std::vector<WalletAddress> newAddresses;
            newAddresses.resize(50);
            for (auto& addr : newAddresses)
            {
                addr.m_label = "contact label";
                addr.m_category = "test category";
                addr.m_createTime = beam::getTimestamp();
                addr.m_duration = std::rand();
                addr.m_OwnID = std::rand();
                db->get_SbbsWalletID(addr.m_BbsAddr, 32);
            }

            for (auto& addr : newAddresses)
            {
                client.getAsync()->saveAddress(addr);
            }
            timer->start(1500, false, [mainReactor]() { mainReactor->stop(); });
        });
        startEvent->post();

        mainReactor->run();
    }

    void TestSendingWithWalletID()
    {
        cout << "\nTesting sending with wallet ID...\n";

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
        TestWalletRig sender(createSenderWalletDB(), f, TestWalletRig::Type::Regular, false, 0);
        TestWalletRig receiver(createReceiverWalletDB(), f);

        vector<Coin> coins;
        vector<ShieldedCoin> shieldedCoins;
        sender.m_WalletDB->selectCoins2(0, 6, Zero, coins, shieldedCoins, 0, false);
        WALLET_CHECK(coins.size() == 2);
        WALLET_CHECK(sender.m_WalletDB->getTxHistory().empty());
        WALLET_CHECK(receiver.m_WalletDB->getTxHistory().empty());

        //completedCount = 1;
        //auto txId1 = sender.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
        //    .SetParameter(TxParameterID::MyAddr, sender.m_BbsAddr)
        //    .SetParameter(TxParameterID::MyEndpoint, sender.m_Endpoint)
        //    .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr)
        //    .SetParameter(TxParameterID::PeerEndpoint, sender.m_Endpoint)
        //    .SetParameter(TxParameterID::Amount, Amount(4))
        //    .SetParameter(TxParameterID::Fee, Amount(2))
        //    .SetParameter(TxParameterID::Lifetime, Height(200))
        //    .SetParameter(TxParameterID::PeerResponseTime, Height(20)));
        //
        //mainReactor->run();
        //
        //// Tx history check
        //auto stx1 = sender.m_WalletDB->getTx(txId1);
        //WALLET_CHECK(stx1);
        //
        //WALLET_CHECK(stx1->m_status == wallet::TxStatus::Failed);

        auto txId = sender.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
            .SetParameter(TxParameterID::MyAddr, sender.m_BbsAddr)
            .SetParameter(TxParameterID::MyEndpoint, sender.m_Endpoint)
            .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr)
            .SetParameter(TxParameterID::PeerEndpoint, receiver.m_Endpoint)
            .SetParameter(TxParameterID::Amount, Amount(4))
            .SetParameter(TxParameterID::Fee, Amount(2))
            .SetParameter(TxParameterID::Lifetime, Height(200))
            .SetParameter(TxParameterID::PeerResponseTime, Height(20)));

        mainReactor->run();
         
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
        WALLET_CHECK(stx->m_status == wallet::TxStatus::Completed);
        WALLET_CHECK(stx->m_fee == rtx->m_fee);
        WALLET_CHECK(stx->m_message == rtx->m_message);
        WALLET_CHECK(stx->m_createTime <= rtx->m_createTime);
        WALLET_CHECK(stx->m_status == rtx->m_status);
        WALLET_CHECK(stx->m_sender == true);
        WALLET_CHECK(rtx->m_sender == false);
    }

    void StoreShieldedCoins(uint32_t nShieldedCoins, Amount nValNetto, const IWalletDB::Ptr walletDb, TestNode& node, Asset::ID assetID = 0)
    {

        ECC::Point::Native ptN = ECC::Context::get().H * 1234U; // random point
        ECC::Point::Storage ptS;
        ptN.Export(ptS);

        node.m_vShieldedPool.resize(50, ptS);

        // calculate shielded element commitment

        Key::Index nShIdx = 0;
        ShieldedTxo::Viewer viewer;
        viewer.FromOwner(*walletDb->get_OwnerKdf(), nShIdx);

        for (uint32_t i = 0; i < nShieldedCoins; i++)
        {
            wallet::ShieldedCoin sc;

            ShieldedTxo::Ticket tkt;
            ShieldedTxo::DataParams sdp;
            sdp.m_Ticket.Generate(tkt, viewer, 12323U + i);
            sc.m_CoinID.m_Key.m_kSerG = sdp.m_Ticket.m_pK[0];
            sc.m_CoinID.m_AssetID = assetID;
            sdp.m_Output.m_Value = sc.m_CoinID.m_Value;
            sdp.m_Output.m_AssetID = sc.m_CoinID.m_AssetID;
            sdp.m_Output.m_User = sc.m_CoinID.m_User;

            sdp.m_Output.Restore_kG(sdp.m_Ticket.m_SharedSecret);

            WALLET_CHECK(ptN.Import(tkt.m_SerialPub));

            ptN += ECC::Context::get().G * sdp.m_Output.m_k;
            CoinID::Generator(sdp.m_Output.m_AssetID).AddValue(ptN, sdp.m_Output.m_Value);

            ptN.Export(ptS);

            sc.m_CoinID.m_Key.m_IsCreatedByViewer = true;
            sc.m_CoinID.m_Key.m_nIdx = nShIdx;
            ZeroObject(sc.m_CoinID.m_User);
            sc.m_TxoID = 12 + i;
            sc.m_CoinID.m_Value = nValNetto;
            sc.m_confirmHeight = 0;

            node.m_vShieldedPool[sc.m_TxoID] = ptS;

            walletDb->saveShieldedCoin(sc);
        }
    }

    void TestSendingShielded()
    {
        cout << "\nTesting consuming shielded TXOs...\n";

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
        TestWalletRig sender(createSenderWalletDB(), f, TestWalletRig::Type::Regular, false, 0);
        TestWalletRig receiver(createReceiverWalletDB(), f);

        auto& fs = Transaction::FeeSettings::get(node.GetHeight());

        const uint32_t nShieldedCoins = 3;
        Amount nValNetto = 135;
        Amount nStdFee = fs.get_DefaultStd();
        StoreShieldedCoins(nShieldedCoins, fs.m_ShieldedInputTotal + (nValNetto + nStdFee)/3 + 1, sender.m_WalletDB, node);

        auto txId  = sender.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
            .SetParameter(TxParameterID::MyAddr, sender.m_BbsAddr)
            .SetParameter(TxParameterID::MyEndpoint, sender.m_Endpoint)
            .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr)
            .SetParameter(TxParameterID::PeerEndpoint, receiver.m_Endpoint)
            .SetParameter(TxParameterID::Amount, nValNetto)
            .SetParameter(TxParameterID::Fee, nStdFee)
            .SetParameter(TxParameterID::Lifetime, Height(200))
            .SetParameter(TxParameterID::PeerResponseTime, Height(20)));

        mainReactor->run();

        // check Tx
        auto txHistory = sender.m_WalletDB->getTxHistory();
        WALLET_CHECK(txHistory.size() == 1);
        WALLET_CHECK(txHistory[0].m_txId == txId);
        WALLET_CHECK(txHistory[0].m_status == wallet::TxStatus::Completed);
    }

    void TestCalculateCoinsSelection()
    {
        cout << "\nTesting coins selection...\n";

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
        AmountList lst;
        auto walletDB = createSenderWalletDB(false, lst);
        StoreShieldedCoins(3, 3000000, walletDB, node);
        auto& fs = Transaction::FeeSettings::get(1);
        Amount beforehandFee = 100;

        wallet::CoinsSelectionInfo csi;
        csi.m_assetID = 0;
        csi.m_requestedSum = 6000000;
        csi.m_explicitFee = beforehandFee;
        csi.Calculate(1, walletDB);

        WALLET_CHECK(!csi.m_isEnought);
        WALLET_CHECK(csi.get_NettoValue() < 6000000);
        WALLET_CHECK(csi.m_involuntaryFee == fs.m_ShieldedInputTotal * 3);

        csi.m_requestedSum = 4000000;
        csi.Calculate(1, walletDB);
        WALLET_CHECK(csi.get_NettoValue() == 4000000);
        WALLET_CHECK(csi.m_involuntaryFee == fs.m_ShieldedInputTotal * 3);
        WALLET_CHECK(csi.m_changeBeam != 0);
        
        csi.m_requestedSum = 3000000;
        csi.Calculate(1, walletDB, true);

        WALLET_CHECK(csi.get_NettoValue() == 3000000);
        WALLET_CHECK(csi.m_involuntaryFee == fs.m_ShieldedInputTotal * 3);
        WALLET_CHECK(csi.m_explicitFee > beforehandFee);

        // assets

        csi.m_requestedSum = 1000;
        csi.m_assetID = 12;
        csi.m_explicitFee = 0;

        csi.Calculate(1, walletDB);
        WALLET_CHECK(!csi.m_isEnought);
        WALLET_CHECK(csi.get_NettoValue() < 1000);
        WALLET_CHECK(csi.m_involuntaryFee == fs.m_ShieldedInputTotal);

        csi.Calculate(1, walletDB, true);
        WALLET_CHECK(!csi.m_isEnought);
        WALLET_CHECK(csi.get_NettoValue() < 1000);
        WALLET_CHECK(csi.m_involuntaryFee == fs.m_ShieldedInputTotal);

        for (int i = 0; i < 10; ++i)
        {
            Coin coin = CreateAvailCoin(300, 0);
            coin.m_ID.m_AssetID = 12;
            walletDB->storeCoin(coin);
        }

        csi.m_requestedSum = 1000;
        csi.m_assetID = 12;
        csi.m_explicitFee = 0;

        csi.Calculate(1, walletDB);
        WALLET_CHECK(csi.m_isEnought);
        WALLET_CHECK(csi.get_NettoValue() == 1000);
        WALLET_CHECK(csi.m_involuntaryFee == fs.m_ShieldedInputTotal);
        WALLET_CHECK(csi.m_changeAsset == 200);
        WALLET_CHECK(csi.m_explicitFee == fs.get_DefaultStd());


        csi.Calculate(1, walletDB, true);
        WALLET_CHECK(csi.m_isEnought);
        WALLET_CHECK(csi.get_NettoValue() == 1000);
        WALLET_CHECK(csi.m_involuntaryFee == fs.m_ShieldedInputTotal);
        WALLET_CHECK(csi.m_changeAsset == 200);
        WALLET_CHECK(csi.m_explicitFee == fs.get_DefaultShieldedOut());

        cout << "\nCoins selection tested\n";
    }

    void TestCalculateAssetCoinsSelection()
    {
        cout << "\nTesting asset coins selection...\n";

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
        AmountList lst = {3000000, 3000000, 3000000};
        auto walletDB = createSenderWalletDB(false, lst);
        StoreShieldedCoins(3, 3000000, walletDB, node, 12);
        auto& fs = Transaction::FeeSettings::get(1);
        Amount beforehandFee = 100;

        wallet::CoinsSelectionInfo csi;
        csi.m_assetID = 12;
        csi.m_requestedSum = 10000000;
        csi.m_explicitFee = beforehandFee;
        csi.Calculate(1, walletDB);

        WALLET_CHECK(!csi.m_isEnought);
        WALLET_CHECK(csi.get_NettoValue() < 10000000);
        WALLET_CHECK(csi.m_involuntaryFee == fs.m_ShieldedInputTotal * 3);

        csi.m_requestedSum = 4000000;
        csi.Calculate(1, walletDB);
        WALLET_CHECK(csi.get_NettoValue() == 4000000);
        WALLET_CHECK(csi.m_involuntaryFee == fs.m_ShieldedInputTotal * 2);
        WALLET_CHECK(csi.m_changeBeam != 0);

        csi.m_requestedSum = 3000000;
        csi.Calculate(1, walletDB, true);

        WALLET_CHECK(csi.get_NettoValue() == 3000000);
        WALLET_CHECK(csi.m_involuntaryFee == fs.m_ShieldedInputTotal * 1);
        WALLET_CHECK(csi.m_explicitFee > beforehandFee);

        cout << "\nCoins selection tested\n";
    }

    void TestMultiUserWallet()
    {
        cout << "\nTesting mulituser wallet...\n";

        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);
        const size_t UserCount = 10;

        size_t completedCount = UserCount * 2;
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

        auto serviceDB = createSenderWalletDB();

        
        std::vector <std::unique_ptr<TestWalletRig>> wallets;

        wallets.reserve(UserCount);
        
        for (size_t i = 0; i < UserCount; ++i)
        {
            stringstream ss;
            ss << "sender_" << i << ".db";
            auto t = make_unique<TestWalletRig>(createSenderWalletDBWithSeed(ss.str(), true), f, TestWalletRig::Type::Regular, false, 0);
            wallets.push_back(std::move(t));
        }
        
        TestWalletRig receiver(createReceiverWalletDB(), f);

 //       WALLET_CHECK(sender.m_WalletDB->selectCoins(6, Zero).size() == 2);
 //       WALLET_CHECK(sender.m_WalletDB->getTxHistory().empty());
 //       WALLET_CHECK(receiver.m_WalletDB->getTxHistory().empty());

        for (auto& w : wallets)
        {
            auto& sender = *w;
            sender.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
                .SetParameter(TxParameterID::MyAddr, sender.m_BbsAddr)
                .SetParameter(TxParameterID::MyEndpoint, sender.m_Endpoint)
                .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr)
                .SetParameter(TxParameterID::PeerEndpoint, receiver.m_Endpoint)
                .SetParameter(TxParameterID::Amount, Amount(4))
                .SetParameter(TxParameterID::Fee, Amount(2))
                .SetParameter(TxParameterID::Lifetime, Height(200))
                .SetParameter(TxParameterID::PeerResponseTime, Height(20)));

        }
        
        mainReactor->run();

        vector<Coin> newReceiverCoins = receiver.GetCoins();

        WALLET_CHECK(newReceiverCoins.size() == UserCount);
        for (auto& coin : newReceiverCoins)
        {
            WALLET_CHECK(coin.m_ID.m_Value == 4);
            WALLET_CHECK(coin.m_status == Coin::Available);
            WALLET_CHECK(coin.m_ID.m_Type == Key::Type::Regular);
        }

        for (auto& w : wallets)
        {
            auto& sender = *w;
            // check coins
            vector<Coin> newSenderCoins = sender.GetCoins();

            WALLET_CHECK(newSenderCoins.size() == 4);

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
    }

    void TestContractInvoke()
    {
        cout << "\nTesting contract tx...\n";

        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);

        auto f = [mainReactor](auto) {
            mainReactor->stop();
        };
        TestWalletRig sender(createSenderWalletDB(), f, TestWalletRig::Type::Regular, false, 0);

        TestNode node;

        bvm2::ContractInvokeData vData;
        bvm2::ContractInvokeEntry& cdata = vData.m_vec.emplace_back();

        cdata.m_Cid = 746U;
        cdata.m_iMethod = 6;
        cdata.m_Args.resize(15, 1);
        cdata.m_Spend[0] = -1300200;
        cdata.m_Spend[2] = -500;
        cdata.m_vSig.emplace_back() = 233U;
        cdata.m_vSig.emplace_back() = 2330U;
        cdata.m_Charge = 600;

        auto txId  = sender.m_Wallet->StartTransaction(
            CreateTransactionParameters(TxType::Contract)
            .SetParameter(TxParameterID::ContractDataPacked, vData));

        mainReactor->run();

        // check Tx
        auto txHistory = sender.m_WalletDB->getTxHistory(TxType::Contract);
        WALLET_CHECK(txHistory.size() == 1);
        WALLET_CHECK(txHistory[0].m_txId == txId);
        WALLET_CHECK(txHistory[0].m_status == wallet::TxStatus::Completed);
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
		v.Set(Rules::get().pForks[1].m_Height, Codes::Scheme);
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
			for (Storage::Map::iterator it = gwOut.begin(); gwOut.end() != it; ++it)
			{
				ByteBuffer& buf = it->second;
				uint32_t code = it->first;
				nSize += sizeof(code) + sizeof(uint32_t) + buf.size();
				gwFin.Send(code, std::move(buf));
			}

			cout << "\t" << chThis << " -> " << chOther << ' ' << nSize << " bytes" << std::endl;


			for (Storage::Map::iterator it = gwOut.begin(); gwOut.end() != it; ++it)
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

		if (Negotiator::Status::Success != status)
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

Amount SetCids(beam::Negotiator::IBase& neg, const Amount* p, size_t n, uint32_t code, uint32_t i0 = 0)
{
	std::vector<CoinID> vec;
	vec.resize(n);
	Amount sum = 0;

	for (size_t i = 0; i < n; i++)
	{
		CoinID& cid = vec[i];
		cid = Zero;

		cid.m_Type = Key::Type::Regular;
		cid.m_Idx = i0 + static_cast<uint32_t>(i);

        cid.m_Value = p[i];
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

		CoinID cid(Zero);
		cid.m_Value = valMSig;
		cid.m_Idx = 500;
		cid.m_Type = FOURCC_FROM(msg2);
		v.Set(cid, Multisig::Codes::Cid);

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

	fee += SetCids(pT2[0], pIn0, _countof(pIn0), MultiTx::Codes::InpCids);
	fee -= SetCids(pT2[0], pOut0, _countof(pOut0), MultiTx::Codes::OutpCids, 700);

	fee += SetCids(pT2[1], pIn1, _countof(pIn1), MultiTx::Codes::InpCids);
	fee -= SetCids(pT2[1], pOut1, _countof(pOut1), MultiTx::Codes::OutpCids, 500);

	for (size_t i = 0; i < _countof(pT2); i++)
	{
		IBase& v = pT2[i];
		v.Set(fee, MultiTx::Codes::KrnFee);

		CoinID cid(Zero);
		cid.m_Value = valMSig;
		cid.m_Idx = 500;
		cid.m_Type = FOURCC_FROM(msg2);
		v.Set(cid, MultiTx::Codes::InpMsCid);

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
		CoinID ms0(Zero);
		ms0.m_Value = valMSig;
		ms0.m_Idx = 500;
		ms0.m_Type = FOURCC_FROM(msg2);

		CoinID ms1 = ms0;
		ms1.m_Idx = 800;

		ECC::Point comm0;
		WALLET_CHECK(pT1[i].Get(comm0, Multisig::Codes::Commitment));


		std::vector<CoinID> vec;
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
		CoinID m_msMy;
        CoinID m_msPeer;
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

		std::vector<CoinID> vIn, vOutWd;
		vIn.resize(1, Zero);
		vIn[0].m_Idx = 215;
		vIn[0].m_Type = Key::Type::Regular;
		vIn[0].m_Value = nMyValue;

		vOutWd.resize(1, Zero);
		vOutWd[0].m_Idx = 216;
		vOutWd[0].m_Type = Key::Type::Regular;
		vOutWd[0].m_Value = nMyValue;

		CoinID ms0(Zero);
		ms0.m_Value = valMSig;
		ms0.m_Type = FOURCC_FROM(msg2);
		ms0.m_Idx = 220;

		CoinID msA = ms0;
		msA.m_Idx++;
		CoinID msB = msA;
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

		CoinID ms0;
		ECC::Point comm0;
		{
			ChannelOpen::Worker wrk2(pT4[i]);
			pT4[i].m_MSig.Get(comm0, Multisig::Codes::Commitment);
			pT4[i].m_MSig.Get(ms0, Multisig::Codes::Cid);
		}

        CoinID msA = ms0;
		msA.m_Idx += 15;
		CoinID msB = msA;
		msB.m_Idx++;

		std::vector<CoinID> vOutWd;
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

void TestKeyKeeper(IPrivateKeyKeeper2::Ptr externalKeyKeeper = {}, size_t index = 3)
{
    struct Peer
    {
        IPrivateKeyKeeper2::Ptr m_pKk;
        EndpointIndex m_KeyID;
        PeerID m_ID;
    };

    struct MyKeeKeeper
        :public LocalPrivateKeyKeeperStd
    {
        using LocalPrivateKeyKeeperStd::LocalPrivateKeyKeeperStd;

        bool IsTrustless() override { return true; }
    };

    Peer pPeer[2];

    for (size_t i = 0; i < _countof(pPeer); i++)
    {
        Key::IKdf::Ptr pKdf;
        HKdf::Create(pKdf, 12345U + i); // random kdf

        Peer& p = pPeer[i];
        if (i != index)
        {
            p.m_pKk = std::make_shared<MyKeeKeeper>(pKdf);
            Cast::Up<MyKeeKeeper>(*p.m_pKk).m_State.m_hvLast = 334U + i;
        }
        else
        {
            p.m_pKk = externalKeyKeeper;
        }

        p.m_KeyID = 14 + i;

        // get ID
        IPrivateKeyKeeper2::Method::get_Kdf m;
        m.m_Type = IPrivateKeyKeeper2::KdfType::Root;
        WALLET_CHECK(IPrivateKeyKeeper2::Status::Success == p.m_pKk->InvokeSync(m));
        WALLET_CHECK(!m.m_pKdf); // we're testing in trustless mode
        WALLET_CHECK(m.m_pPKdf);

        Hash::Value hv;
        Key::ID(p.m_KeyID, Key::Type::EndPoint).get_Hash(hv);

        Point::Native ptN;
        m.m_pPKdf->DerivePKeyG(ptN, hv);

        Point pt = ptN;
        p.m_ID = pt.m_X;
    }

    Peer& s = pPeer[0];
    Peer& r = pPeer[1];

    // 1st sender invocation
    IPrivateKeyKeeper2::Method::SignSender mS;
    ZeroObjectUnchecked(mS); // not so good coz it contains vectors. nevermind, it's a test
    mS.m_Peer = r.m_ID;
    mS.m_iEndpoint = s.m_KeyID;
    mS.m_Slot = 12;
    mS.m_pKernel.reset(new TxKernelStd);
    mS.m_pKernel->m_Fee = 315;
    mS.m_pKernel->m_Height.m_Min = Rules::get().pForks[1].m_Height + 19;
    mS.m_pKernel->m_Height.m_Max = mS.m_pKernel->m_Height.m_Min + 700;
    mS.m_vInputs.push_back(CoinID(515, 2342, Key::Type::Regular, 11));
    mS.m_vOutputs.push_back(CoinID(70, 2343, Key::Type::Change));

    WALLET_CHECK(IPrivateKeyKeeper2::Status::Success == s.m_pKk->InvokeSync(mS));

    // Receiver invocation
    IPrivateKeyKeeper2::Method::SignReceiver mR;
    ZeroObjectUnchecked(mR);
    mR.m_Peer = s.m_ID;
    mR.m_iEndpoint = r.m_KeyID;
    mR.m_pKernel = std::move(mS.m_pKernel);
    mR.m_vInputs.push_back(CoinID(3, 2344, Key::Type::Regular));
    mR.m_vOutputs.push_back(CoinID(125, 2345, Key::Type::Regular));
    mR.m_vOutputs.push_back(CoinID(8, 2346, Key::Type::Regular));

    // adjust kernel height a little
    mR.m_pKernel->m_Height.m_Min += 2;
    mR.m_pKernel->m_Height.m_Max += 3;

    WALLET_CHECK(IPrivateKeyKeeper2::Status::Success == r.m_pKk->InvokeSync(mR));

    // 2nd sender invocation
    mS.m_pKernel = std::move(mR.m_pKernel);
    mS.m_kOffset = mR.m_kOffset;
    mS.m_PaymentProofSignature = mR.m_PaymentProofSignature;

    WALLET_CHECK(IPrivateKeyKeeper2::Status::Success == s.m_pKk->InvokeSync(mS));

    TxKernelStd::Ptr pKrn = std::move(mS.m_pKernel);
    pKrn->UpdateID();

    Height hScheme = pKrn->m_Height.m_Min;
    WALLET_CHECK(pKrn->IsValid(hScheme));

    // build the whole tx
    Transaction tx;

    for (size_t i = 0; i < _countof(pPeer); i++)
    {
        Peer& p = pPeer[i];

        const IPrivateKeyKeeper2::Method::InOuts& io = i ?
            Cast::Down<IPrivateKeyKeeper2::Method::InOuts>(mR) :
            Cast::Down<IPrivateKeyKeeper2::Method::InOuts>(mS);

        for (size_t j = 0; j < io.m_vInputs.size(); j++)
        {
            const CoinID& cid = io.m_vInputs[j];

            // build input commitment
            IPrivateKeyKeeper2::Method::get_Commitment m;
            m.m_Cid = cid;
            WALLET_CHECK(IPrivateKeyKeeper2::Status::Success == p.m_pKk->InvokeSync(m));

            tx.m_vInputs.emplace_back();
            tx.m_vInputs.back().reset(new Input);
            tx.m_vInputs.back()->m_Commitment = m.m_Result;
        }

        for (size_t j = 0; j < io.m_vOutputs.size(); j++)
        {
            IPrivateKeyKeeper2::Method::CreateOutput m;
            m.m_Cid = io.m_vOutputs[j];
            m.m_hScheme = hScheme;
            WALLET_CHECK(IPrivateKeyKeeper2::Status::Success == p.m_pKk->InvokeSync(m));

            tx.m_vOutputs.push_back(std::move(m.m_pResult));
        }
    }

    tx.m_vKernels.push_back(std::move(pKrn));
    tx.m_Offset = mS.m_kOffset;
    tx.Normalize();

    Transaction::Context ctx;
    ctx.m_Height.m_Min = hScheme;
    WALLET_CHECK(tx.IsValid(ctx));
}

void TestArgumentParsing()
{
    struct MyProcessor : bvm2::ProcessorManager
    {
        virtual void SelectContext(bool bDependent, uint32_t nChargeNeeded) override {}
    };
    {
        MyProcessor p;
        p.AddArgs(R"( role=manager, action    =destroy_contract, cid = 2dd39c06ede9c97e944b8393a7efb2d0b04d1ffc4a6d97a95f0111cff2d , name="my \"trt,ywy\" name", te_t = "saa ,  "    )");
        WALLET_CHECK(p.m_Args.size() == 5);
        WALLET_CHECK(p.m_Args["role"] == "manager");
        WALLET_CHECK(p.m_Args["action"] == "destroy_contract");
        WALLET_CHECK(p.m_Args["cid"] == "2dd39c06ede9c97e944b8393a7efb2d0b04d1ffc4a6d97a95f0111cff2d");
        WALLET_CHECK(p.m_Args["name"] == "my \"trt,ywy\" name");
        WALLET_CHECK(p.m_Args["te_t"] == "saa ,  ");
    }
    {
        MyProcessor p;
        p.AddArgs(R"(action = create, metadata = STD:SCH_VER=1;N=bU_Coin;SN=bU;UN=bU;NTHUN=AGROTH)");
        WALLET_CHECK(p.m_Args.size() == 2);
        WALLET_CHECK(p.m_Args["action"] == "create");
        WALLET_CHECK(p.m_Args["metadata"] == "STD:SCH_VER=1;N=bU_Coin;SN=bU;UN=bU;NTHUN=AGROTH");
    }

    {
        MyProcessor p;
        p.AddArgs(R"(action = create, metadata = STD:SCH_VER=1;N=bU_Coin;SN=bU;UN=bU;NTHUN=AGROTH , test=name  )");
        WALLET_CHECK(p.m_Args.size() == 3);
        WALLET_CHECK(p.m_Args["action"] == "create");
        WALLET_CHECK(p.m_Args["metadata"] == "STD:SCH_VER=1;N=bU_Coin;SN=bU;UN=bU;NTHUN=AGROTH");
        WALLET_CHECK(p.m_Args["test"] == "name");
    }

    {
        MyProcessor p;
        p.AddArgs(R"(action = create, metadata = "STD:SCH_VER=1;N=bU_Coin;SN=bU;UN=bU;NTHUN=AGROTH")");
        WALLET_CHECK(p.m_Args.size() == 2);
        WALLET_CHECK(p.m_Args["action"] == "create");
        WALLET_CHECK(p.m_Args["metadata"] == "STD:SCH_VER=1;N=bU_Coin;SN=bU;UN=bU;NTHUN=AGROTH");
    }
    {
        std::string largeData;
        constexpr size_t maxSize = 2'000'000;
        largeData.reserve(maxSize);
        largeData.append("data=");
        for (size_t i = largeData.size(); i < maxSize; ++i)
        {
            largeData.push_back('A');
        }
        MyProcessor p;
        p.AddArgs(largeData);
        WALLET_CHECK(p.m_Args.size() == 1);
        const std::string& data = p.m_Args["data"];
        WALLET_CHECK(std::all_of(data.cbegin(), data.cend(), [](char c) { return c == 'A'; }) == true);
    }
    {
        std::string largeData;
        constexpr size_t maxSize = 2'000'000;
        largeData.reserve(maxSize);
        largeData.append("data=\"");
        for (size_t i = largeData.size(); i < maxSize; ++i)
        {
            largeData.push_back('A');
        }
        largeData.push_back('\"');
        MyProcessor p;
        p.AddArgs(largeData);
        WALLET_CHECK(p.m_Args.size() == 1);
        const std::string& data = p.m_Args["data"];
        WALLET_CHECK(std::all_of(data.cbegin(), data.cend(), [](char c) { return c == 'A'; }) == true);
    }
    {
        std::string largeData;
        constexpr size_t maxSize = 2'000'000;
        largeData.reserve(maxSize);
        largeData.append("data=\"");
        for (size_t i = largeData.size(); i < maxSize; ++i)
        {
            largeData.push_back('A');
        }
        MyProcessor p;
        p.AddArgs(largeData);
        WALLET_CHECK(p.m_Args.empty());
    }
    {
        MyProcessor p;
        p.AddArgs(R"(role=manager,action=destroy_contract,cid=2dd39c06ede9c97e944b8393a7efb2d0b04d1ffc4a6d97a95f0111cff2d,name="my \"trt,ywy\" name",te_t = "saa ,  "    )");
        WALLET_CHECK(p.m_Args.size() == 5);
        WALLET_CHECK(p.m_Args["role"] == "manager");
        WALLET_CHECK(p.m_Args["action"] == "destroy_contract");
        WALLET_CHECK(p.m_Args["cid"] == "2dd39c06ede9c97e944b8393a7efb2d0b04d1ffc4a6d97a95f0111cff2d");
        WALLET_CHECK(p.m_Args["name"] == "my \"trt,ywy\" name");
        WALLET_CHECK(p.m_Args["te_t"] == "saa ,  ");
    }

    {
        MyProcessor p;
        p.AddArgs(R"e(role=manager,action=destroy_contract,data="{\"name\": \"Otton I\", \"age\":  12}")e");
        WALLET_CHECK(p.m_Args.size() == 3);
        WALLET_CHECK(p.m_Args["role"] == "manager");
        WALLET_CHECK(p.m_Args["action"] == "destroy_contract");
        WALLET_CHECK(p.m_Args["data"] == "{\"name\": \"Otton I\", \"age\":  12}");

    }
    {
        MyProcessor p;
        p.AddArgs("");
        WALLET_CHECK(p.m_Args.empty());
    }

    {
        MyProcessor p;
        p.AddArgs("r54ole=manager,act ion=destroy_contract,cid=2dd39c06e653563 543536 76 76��������������;;;;';'df;;.,,,,,,de9c97e944b8393a7efb2d0b04d1ffc4a6d97a95f0111cff2d,na ");
        WALLET_CHECK(p.m_Args.size() == 3);
        WALLET_CHECK(p.m_Args["r54ole"] == "manager");
        WALLET_CHECK(p.m_Args["ion"] == "destroy_contract");
        WALLET_CHECK(p.m_Args["cid"] == "2dd39c06e653563");
    }

    {
        MyProcessor p;
        p.AddArgs("r54ole=manager,act ion=destroy_contract,cid=2dd39c06e653563   ");
        WALLET_CHECK(p.m_Args.size() == 3);
        WALLET_CHECK(p.m_Args["r54ole"] == "manager");
        WALLET_CHECK(p.m_Args["ion"] == "destroy_contract");
        WALLET_CHECK(p.m_Args["cid"] == "2dd39c06e653563");
    }

    {
        MyProcessor p;
        p.AddArgs("data=\"role=manager,action=destroy_contract,cid=2dd39c06e653563    \"");
        WALLET_CHECK(p.m_Args.size() == 1);
        WALLET_CHECK(p.m_Args["data"] == "role=manager,action=destroy_contract,cid=2dd39c06e653563    ");
    }

    { // Separate only by comma
        MyProcessor p;
        p.AddArgs(R"(action = create   	  metadata = aaaa)");
        WALLET_CHECK(p.m_Args.size() == 1);
        WALLET_CHECK(p.m_Args["action"] == "create");
    }

    { // Separate only by comma
        MyProcessor p;
        p.AddArgs(R"(action = create   ;    metadata = aaaa)");
        WALLET_CHECK(p.m_Args.size() == 1);
        WALLET_CHECK(p.m_Args["action"] == "create");
    }

    { // Separate only by comma
        MyProcessor p;
        p.AddArgs(R"(action = create   ,    metadata = aaaa)");
        WALLET_CHECK(p.m_Args.size() == 2);
        WALLET_CHECK(p.m_Args["action"] == "create");
        WALLET_CHECK(p.m_Args["metadata"] == "aaaa");
    }

    {
      MyProcessor p;
      p.AddArgs("action = \"some very\nlarge\ntext\nwith\na lot\nof\tlines\"");
      WALLET_CHECK(p.m_Args.size() == 1);
      WALLET_CHECK(p.m_Args["action"] == "some very\nlarge\ntext\nwith\na lot\nof\tlines");
    }

    {
      MyProcessor p;
      p.AddArgs("action = \"some very\nlarge\ntext\nwith\na lot\nof\tlines\"   ,     metadata = aaa");
      WALLET_CHECK(p.m_Args.size() == 2);
      WALLET_CHECK(p.m_Args["action"] == "some very\nlarge\ntext\nwith\na lot\nof\tlines");
      WALLET_CHECK(p.m_Args["metadata"] == "aaa");
    }


    {
      MyProcessor p;
      p.AddArgs(R"(data="some very
large
text
with
a lot
of	lines",role=manager,action=destroy_contract,cid=2dd39c06ede9c97e944b8393a7efb2d0b04d1ffc4a6d97a95f0111cff2d,name="my \"trt,ywy\" name",te_t = "saa ,  "    )");
      WALLET_CHECK(p.m_Args.size() == 6);
      WALLET_CHECK(p.m_Args["data"] == "some very\nlarge\ntext\nwith\na lot\nof\tlines");
      WALLET_CHECK(p.m_Args["role"] == "manager");
      WALLET_CHECK(p.m_Args["action"] == "destroy_contract");
      WALLET_CHECK(p.m_Args["cid"] == "2dd39c06ede9c97e944b8393a7efb2d0b04d1ffc4a6d97a95f0111cff2d");
      WALLET_CHECK(p.m_Args["name"] == "my \"trt,ywy\" name");
      WALLET_CHECK(p.m_Args["te_t"] == "saa ,  ");
    }


    {
      MyProcessor p;
      p.AddArgs(R"e(lines_data="some very
large
text
with
a lot
of	lines",role=manager,action=destroy_contract,data="{\"name\": \"Otton I\", \"age\":  12}")e");
      WALLET_CHECK(p.m_Args.size() == 4);
      WALLET_CHECK(p.m_Args["lines_data"] == "some very\nlarge\ntext\nwith\na lot\nof\tlines");
      WALLET_CHECK(p.m_Args["role"] == "manager");
      WALLET_CHECK(p.m_Args["action"] == "destroy_contract");
      WALLET_CHECK(p.m_Args["data"] == "{\"name\": \"Otton I\", \"age\":  12}");

    }
}

void TestVouchers()
{
    cout << "\nTesting wallets vouchers exchange...\n";

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    TestNodeNetwork::Shared tnns;

    struct MyWallet
        :public Wallet
    {
        std::vector<ShieldedTxo::Voucher> m_Vouchers;
        WalletAddress m_MyAddr;
        TestNodeNetwork::Ptr m_MyNetwork;

        MyWallet(IWalletDB::Ptr pDb, const std::shared_ptr<TestWalletNetwork>& pTwn, TestNodeNetwork::Shared& tnns)
            :Wallet(pDb)
        {
            pDb->createAddress(m_MyAddr);
            pDb->saveAddress(m_MyAddr);

            m_MyNetwork = make_shared<TestNodeNetwork>(tnns, *this);

            AddMessageEndpoint(pTwn);
            SetNodeEndpoint(m_MyNetwork);

            pTwn->m_Map[m_MyAddr.m_BbsAddr].m_pSink = this;

            Cast::Down<FlyClient>(*this).OnOwnedNode(Zero, true); // hack, disable wallet's logic according to which it won't reply with voucher without owned node.
        }

        virtual void OnVouchersFrom(const WalletAddress&, const WalletID& myAddr, std::vector<ShieldedTxo::Voucher>&& res) override
        {
            m_Vouchers = std::move(res);
            io::Reactor::get_Current().stop();
        }
    };

    auto twn = make_shared<TestWalletNetwork>();

    IWalletDB::Ptr pDbSnd = createSenderWalletDB();
    MyWallet sender(pDbSnd, twn, tnns);
    MyWallet receiver(createReceiverWalletDB(), twn, tnns);

    WalletAddress addr = receiver.m_MyAddr;
    addr.m_OwnID = 0;
    pDbSnd->saveAddress(addr);

    sender.RequestVouchersFrom(receiver.m_MyAddr.m_BbsAddr, sender.m_MyAddr.m_BbsAddr, 15);

    io::Timer::Ptr timer = io::Timer::create(*mainReactor);
    timer->start(1000, true, []() { io::Reactor::get_Current().stop(); });

    mainReactor->run();

    WALLET_CHECK(!sender.m_Vouchers.empty());
}

void TestAddressGeneration()
{
    cout << "\nTesting tokens generation exchange...\n";

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    auto db = createSenderWalletDB();

    WalletAddress addr("test"); db->createAddress(addr);
    auto t1 = GenerateToken(TokenType::RegularOldStyle, addr, db);

    WalletID w1;
    WALLET_CHECK(w1.FromHex(t1));
    WALLET_CHECK(GetAddressType(t1) == TxAddressType::Regular);
    
    auto t2 = GenerateToken(TokenType::RegularNewStyle, addr, db);
    WALLET_CHECK(GetAddressType(t2) == TxAddressType::Regular);

    auto p2 = ParseParameters(t2);
    WALLET_CHECK(p2);

    auto peerID2= p2->GetParameter<WalletID>(TxParameterID::PeerAddr);
    WALLET_CHECK(peerID2);

    auto& ww1 = *peerID2;
    WALLET_CHECK(ww1.cmp(w1) == 0);

    auto t3 = GenerateToken(TokenType::Offline, addr, db);
    WALLET_CHECK(GetAddressType(t3) == TxAddressType::Offline);

    auto t4 = GenerateToken(TokenType::MaxPrivacy, addr, db);
    WALLET_CHECK(GetAddressType(t4) == TxAddressType::MaxPrivacy);

    auto t5 = GenerateToken(TokenType::Public, addr, db);
    WALLET_CHECK(GetAddressType(t5) == TxAddressType::PublicOffline);

    const std::string abra = "cadabra";
    addr.m_Token = abra;
    db->saveAddress(addr);

    auto read = db->getAddress(addr.m_BbsAddr);
    WALLET_CHECK(read);
    WALLET_CHECK(read->m_Token == abra);
}

void TestAddressVersions()
{
    cout << "\nTesting tokens versions...\n";

    std::string clientVersion = "Beam UI 9.0.1313.2362";
    std::string libraryVersion = "9.0.1316";

    auto testFunc = [&]()
    {
        TxParameters params;
        params.SetParameter(TxParameterID::ClientVersion, clientVersion);
        params.SetParameter(TxParameterID::LibraryVersion, libraryVersion);

        bool failed = true;
        ProcessLibraryVersion(params, [&](const auto& version, const auto& myVersion)
        {
            WALLET_CHECK(version == "9.0.1316");
            failed = false;
        });
        WALLET_CHECK(!failed);
        failed = true;
        ProcessClientVersion(params, "Beam UI", "6.0.13163.2372", "9.0.1313", [&](const auto& version, const auto& myVersion)
        {
            WALLET_CHECK(version == "9.0.1313.2362");
            failed = false;
        });
        WALLET_CHECK(!failed);
        params.SetParameter(TxParameterID::ClientVersion, std::string("Beam UI " BEAM_LIB_VERSION));
        params.SetParameter(TxParameterID::LibraryVersion, std::string(BEAM_LIB_VERSION));
        failed = false;
        ProcessLibraryVersion(params, [&](const auto& version, const auto& myVersion)
        {
            failed = true;
        });
        WALLET_CHECK(!failed);
        failed = false;
        ProcessClientVersion(params, "Beam UI", BEAM_LIB_VERSION, BEAM_LIB_VERSION, [&](const auto& version, const auto& myVersion)
        {
            failed = true;
        });
        WALLET_CHECK(!failed);
    };

    testFunc();

    ByteBuffer buf = toByteBuffer(Version{ 9, 0, 1316 });
    std::string{ buf.begin(), buf.end() }.swap(libraryVersion);

    buf = toByteBuffer(ClientVersion{ 2362U });
    std::string{ buf.begin(), buf.end() }.swap(clientVersion);
    testFunc();
    
}

#if defined(BEAM_HW_WALLET)

//IWalletDB::Ptr createSqliteWalletDB()
//{
//    const char* dbName = "wallet.db";
//    if (boost::filesystem::exists(dbName))
//    {
//        boost::filesystem::remove(dbName);
//    }
//    ECC::NoLeak<ECC::uintBig> seed;
//    seed.V = Zero;
//    auto walletDB = WalletDB::init(dbName, string("pass123"), seed, io::Reactor::get_Current().shared_from_this());
//    beam::Block::SystemState::ID id = { };
//    id.m_Height = 134;
//    walletDB->setSystemStateID(id);
//    return walletDB;
//}
//
//void TestHWTransaction(IPrivateKeyKeeper& pkk)
//{
//    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
//    io::Reactor::Scope scope(*mainReactor);
//
//    Point::Native totalPublicExcess = Zero;
//
//    std::vector<Coin::ID> inputCoins =
//    {
//        {40, 0, Key::Type::Regular},
//    };
//
//    std::vector<Coin::ID> outputCoins =
//    {
//        {16, 0, Key::Type::Regular},
//        {24, 0, Key::Type::Regular},
//    };
//
//    beam::Amount fee = 0;
//    ECC::Scalar::Native offset = Zero;
//    offset.GenRandomNnz();
//
//    {
//
//        Point::Native publicAmount = Zero;
//        Amount amount = 0;
//        for (const auto& cid : inputCoins)
//        {
//            amount += cid.m_Value;
//        }
//        AmountBig::AddTo(publicAmount, amount);
//        amount = 0;
//        publicAmount = -publicAmount;
//        for (const auto& cid : outputCoins)
//        {
//            amount += cid.m_Value;
//        }
//        AmountBig::AddTo(publicAmount, amount);
//
//        Point::Native publicExcess = Context::get().G * offset;
//
//        {
//            Point::Native commitment;
//
//            for (const auto& output : outputCoins)
//            {
//                if (commitment.Import(pkk.GeneratePublicKeySync(output, Zero, true)))
//                {
//                    publicExcess += commitment;
//                }
//            }
//
//            publicExcess = -publicExcess;
//            for (const auto& input : inputCoins)
//            {
//                if (commitment.Import(pkk.GeneratePublicKeySync(input, Zero, true)))
//                {
//                    publicExcess += commitment;
//                }
//            }
//        }
//
//        publicExcess += publicAmount;
//
//        totalPublicExcess = publicExcess;
//    }
//
//    {
//        ECC::Point::Native peerPublicNonce = Zero;
//
//        TxKernel kernel;
//        kernel.m_Fee = fee;
//        kernel.m_Height = { 25000, 27500 };
//        kernel.m_Commitment = totalPublicExcess;
//
//        ECC::Hash::Value message;
//        kernel.get_Hash(message);
//
//        KernelParameters kernelParameters;
//        kernelParameters.fee = fee;
//
//        kernelParameters.height = { 25000, 27500 };
//        kernelParameters.commitment = totalPublicExcess;
//
//        Signature signature;
//
//        ECC::Point::Native publicNonce;
//        uint8_t nonceSlot = (uint8_t)pkk.AllocateNonceSlotSync();
//        publicNonce.Import(pkk.GenerateNonceSync(nonceSlot));
//
//
//        signature.m_NoncePub = publicNonce + peerPublicNonce;
//        signature.m_k = pkk.SignSync(inputCoins, outputCoins, offset, nonceSlot, kernelParameters, publicNonce + peerPublicNonce);
//
//        if (signature.IsValid(message, totalPublicExcess))
//        {
//            LOG_DEBUG() << "Ok, signature is valid :)";
//        }
//        else
//        {
//            LOG_ERROR() << "Error, invalid signature :(";
//        }
//    }
//}

#include "mnemonic/mnemonic.h"

void TestHWCommitment()
{
    cout << "Test HW commitment" << std::endl;

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    const CoinID cid(100500, 15, Key::Type::Regular, 7);
    const Height hScheme = 100500;
    ECC::NoLeak<ECC::HKdfPub::Packed> owner1, owner2;
    Point comm1, comm2;
    auto extractOwner = [](Key::IPKdf::Ptr pubKdf, ECC::NoLeak<ECC::HKdfPub::Packed>& p)
    {
        assert(pubKdf->ExportP(nullptr) == sizeof(p));
        pubKdf->ExportP(&p);
        const uint8_t* pp = reinterpret_cast<const uint8_t*>(&p.V);
        cout << "Owner bin: " << to_hex(pp, sizeof(p.V)) << std::endl;  
    };
    {
        Scalar::Native secretKey;

        //beam::WordList generatedPhrases = {"budget", "focus", "surface", "plug", "dragon", "elephant", "token", "child", "kitchen", "coast", "lounge", "mean" };
        beam::WordList generatedPhrases = { "copy", "vendor", "shallow", "raven", "coffee", "appear", "book", "blast", "lock", "exchange", "farm", "glue" };
        //phrase that is used in beam-hw-crypto tests
        //beam::WordList generatedPhrases = {"edge", "video", "genuine", "moon", "vibrant", "hybrid", "forum", "climb", "history", "iron", "involve", "sausage"};

        auto buf = beam::decodeMnemonic(generatedPhrases);

        SecString secretSeed;
        secretSeed.assign(buf.data(), buf.size());

        Key::IKdf::Ptr kdf;
        ECC::HKdf::Create(kdf, secretSeed.hash().V);

        Key::IPKdf::Ptr pubKdf = kdf;
        LocalPrivateKeyKeeperStd keyKeeper(kdf);

        extractOwner(pubKdf, owner1);

        ECC::Point::Native pt2;
        WALLET_CHECK(keyKeeper.get_Commitment(pt2, cid) == IPrivateKeyKeeper2::Status::Success);

        comm1 = pt2;
        LOG_INFO() << "commitment is " << comm1;

        
        {
            ECC::Point::Native comm3;
            IPrivateKeyKeeper2::Method::CreateOutput m;
            m.m_Cid = cid;
            m.m_hScheme = hScheme;
            WALLET_CHECK(IPrivateKeyKeeper2::Status::Success == keyKeeper.InvokeSync(m));
            WALLET_CHECK(m.m_pResult && m.m_pResult->IsValid(hScheme, comm3));
            WALLET_CHECK(comm3 == pt2);
        }
    }

    {
        HWWallet hw;
        auto keyKeeper = hw.getKeyKeeper(hw.getDevices()[0]);

        {
            IPrivateKeyKeeper2::Method::get_Kdf m;
            m.m_Root = true;
            WALLET_CHECK(keyKeeper->InvokeSync(m) == IPrivateKeyKeeper2::Status::Success);
            extractOwner(m.m_pPKdf, owner2);
        }

        ECC::Point::Native pt2;
        WALLET_CHECK(keyKeeper->get_Commitment(pt2, cid) == IPrivateKeyKeeper2::Status::Success);
        comm2 = pt2;

        LOG_INFO() << "HW commitment is " << comm2;

        {
            ECC::Point::Native comm3;
            IPrivateKeyKeeper2::Method::CreateOutput m;
            m.m_Cid = cid;
            m.m_hScheme = hScheme;
            WALLET_CHECK(IPrivateKeyKeeper2::Status::Success == keyKeeper->InvokeSync(m));
            WALLET_CHECK(m.m_pResult && m.m_pResult->IsValid(hScheme, comm3));
            WALLET_CHECK(comm3 == pt2);
        }
    }

    WALLET_CHECK(comm1 == comm2);
}

void TestHWWallet()
{
    cout << "Test HW wallet" << std::endl;
    
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    HWWallet hw;

    auto keyKeeper = hw.getKeyKeeper(hw.getDevices()[0]);

    TestKeyKeeper(keyKeeper, 1);
    TestKeyKeeper(keyKeeper, 0);
}
#endif

template <typename T>
void FSave(const T& x, const std::string& sPath)
{
    std::FStream f;
    f.Open(sPath.c_str(), false, true);

    yas::binary_oarchive<std::FStream, SERIALIZE_OPTIONS> arc(f);
    arc& x;
}

void GenerateTreasury(size_t walletCount, size_t utxoCount, Amount value)
{
    std::vector<HKdf::IKdf::Ptr> kdfs;
    Treasury treasury;
    assert(walletCount > 0);

    // generate seeds
    for (size_t i = 0; i < walletCount; ++i)
    {
        auto seedPhrase = createMnemonic(getEntropy());
        for (const auto& word : seedPhrase)
        {
            std::cout << word << ';';
        }
        std::cout << std::endl;
        NoLeak<uintBig> walletSeed;
        SecString seed;
        auto buf = decodeMnemonic(seedPhrase);
        seed.assign(buf.data(), buf.size());

        walletSeed.V = seed.hash().V;
        HKdf::IKdf::Ptr pKdf;
        ECC::HKdf::Create(pKdf, walletSeed.V);

        kdfs.push_back(pKdf);

        // generate plans
        PeerID pid;
        ECC::Scalar::Native sk;

        Treasury::get_ID(*pKdf, pid, sk);

        Treasury::Parameters params;
        params.m_Bursts = 1U;
        params.m_Maturity0 = 1;
        params.m_MaturityStep = 1;

        // create plan

        auto it = treasury.m_Entries.find(pid);
        if (treasury.m_Entries.end() != it)
            treasury.m_Entries.erase(it);

        auto& e = treasury.m_Entries[pid];
        auto& r = e.m_Request;
        r.m_WalletID = pid;

        HeightRange hr;
        hr.m_Max = params.m_Maturity0 + Rules::HeightGenesis - 1;

        hr.m_Min = hr.m_Max + 1;
        hr.m_Max += params.m_MaturityStep;

        AmountBig::Type valBig;
        Rules::get_Emission(valBig, hr, value);
        if (AmountBig::get_Hi(valBig))
            throw std::runtime_error("too large");

        Amount val = AmountBig::get_Lo(valBig);

        r.m_vGroups.emplace_back();
        for (size_t j = 0; j < utxoCount; ++j)
        {
            auto& c = r.m_vGroups.back().m_vCoins.emplace_back();
            c.m_Incubation = hr.m_Max;
            c.m_Value = val;
        }

        ///

        Treasury::Entry* plan = &e;
        plan->m_pResponse.reset(new Treasury::Response);
        uint64_t nIndex = 1;
        plan->m_pResponse->Create(plan->m_Request, *pKdf, nIndex);
        WALLET_CHECK(plan->m_pResponse->IsValid(plan->m_Request));
    }


    Treasury::Data data;
    data.m_sCustomMsg = "test treasury";
    treasury.Build(data);

    FSave(data, "treasury.bin");
}

template<typename T>
void TestThread()
{
    struct Counter
    {
        void Increment()
        {
            std::unique_lock lock(m_Mutex);
            ++value;
        }

        int value = 0;
        std::mutex m_Mutex;
    } counter;

    auto f = [&counter](int id, unsigned interval)
    {
        std::stringstream ss;
        ss << "thread " << id;
        std::cout << ss.str() << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(interval));
        counter.Increment();
        ss << " done";
        std::cout << ss.str() << std::endl;
    };

    T t1(f, 1, 410);

    T t2(f, 2, 150);
    WALLET_CHECK(t2.joinable());
    T tt = std::move(t2);
    WALLET_CHECK(!t2.joinable());
    WALLET_CHECK(tt.joinable());
    t1.join();
    tt.join();

    WALLET_CHECK(counter.value == 2);
    {
        T t3(f, 3, 150);
        t3.detach();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    WALLET_CHECK(counter.value == 3);

    struct Test
    {
        Test()
            : m_Thread(T(&Test::Thread, this, std::cref(Rules::get())))
        {

        }

        ~Test()
        {
            WALLET_CHECK(m_Thread.joinable());
            if (m_Thread.joinable())
                m_Thread.join();
        }

        void Thread(const Rules& r)
        {
            std::cout << "thread 4" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            std::cout << "thread 4 Done" << std::endl;
        }

        T m_Thread;
    };
    Test t;

}

void TestThreadPool()
{
    std::cout << "Testing simple thread pool\n";
    // Pool thread should be similar to std::thread and should pass the same test
    TestThread<PoolThread>();
    TestThread<std::thread>();
}

int main()
{
    int logLevel = LOG_LEVEL_WARNING; 
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    const auto path = boost::filesystem::system_complete("logs");
    auto logger = beam::Logger::create(logLevel, logLevel, logLevel, "wallet_test", path.string());

    ECC::PseudoRandomGenerator prg;
    ECC::PseudoRandomGenerator::Scope scopePrg(&prg);
    prg.m_hv = 125U;


    Rules::get().FakePoW = true;
	Rules::get().pForks[1].m_Height = 100500; // needed for lightning network to work
    Rules::get().DisableForksFrom(2);
    Rules::get().UpdateChecksum();

    wallet::g_AssetsEnabled = true;

    storage::HookErrors();
    TestArgumentParsing();
    TestThreadPool();
    //GenerateTreasury(100, 100, 100000000);
    TestTxList();
    TestKeyKeeper();

    TestVouchers();

    TestAddressGeneration();
    TestAddressVersions();

    //TestBbsDecrypt();

    TestConvertions();
    TestTxParameters();
    
    TestClient();
    TestSendingWithWalletID();
    
    TestMultiUserWallet();
    
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
    TestNoResponse();
    TestTransactionUpdate();
    //TestTxPerformance();
    //TestTxNonces();
    
    TestTxExceptionHandling();
    
   
    // @nesbox: disabled tests, they work only if device connected
#if defined(BEAM_HW_WALLET)
    TestHWCommitment();
    TestHWWallet();
#endif

    //TestBbsMessages();
    //TestBbsMessages2();

    Rules::get().pForks[1].m_Height = 1;
    Rules::get().pForks[2].m_Height = 1;
    Rules::get().pForks[3].m_Height = 1;
    Rules::get().pForks[4].m_Height = 1;
    Rules::get().UpdateChecksum();
    TestAppShader1();
    TestAppShader2();
    TestAppShader3();
    TestAppShader4();
    Rules::get().pForks[1].m_Height = 20;
    Rules::get().pForks[2].m_Height = 20;
    Rules::get().pForks[3].m_Height = 20;
    Rules::get().pForks[4].m_Height = 20;
    Rules::get().UpdateChecksum();

    TestSendingShielded();
    TestCalculateCoinsSelection();
    TestCalculateAssetCoinsSelection();

    TestContractInvoke();

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}
