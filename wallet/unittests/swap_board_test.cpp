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


#include <assert.h>
#include <iostream>
#include <map>
#include <functional>

#include <boost/filesystem.hpp>

#include "test_helpers.h"
#include "wallet/common.h"
#include "wallet/wallet_network.h"
#include "keykeeper/local_private_key_keeper.h"

// for wallet_test_environment.cpp
#include "core/radixtree.h"
#include "utility/test_helpers.h"
#include "node/node.h"

#include <boost/filesystem.hpp>

#include "test_helpers.h"
#include "wallet/common.h"
#include "wallet/wallet_network.h"
#include "keykeeper/local_private_key_keeper.h"

// for wallet_test_environment.cpp
#include "core/unittest/mini_blockchain.h"
#include "core/radixtree.h"
#include "utility/test_helpers.h"
#include "node/node.h"

WALLET_TEST_INIT

#include "wallet_test_environment.cpp"

#include "wallet/swaps/swap_offers_board.h"
#include "wallet/swaps/swap_offers_board.cpp"

using namespace beam;
using namespace beam::wallet;
using namespace std;


namespace
{
    /**
     *  Class to test correct notification of SwapOffersBoard observers
     */
    struct MockBoardObserver : public ISwapOffersObserver
    {
        MockBoardObserver(function<void(ChangeAction, const vector<SwapOffer>&)> checker) :
            m_testChecker(checker) {};

        virtual void onSwapOffersChanged(ChangeAction action, const vector<SwapOffer>& offers) override
        {
            m_testChecker(action, offers);
        }

        function<void(ChangeAction, const vector<SwapOffer>&)> m_testChecker;
    };

    /**
     *  Real Wallet implementation isn't used in this test.
     *  Mock used to for BaseMessageEndpoint constructor.
     */
    struct MockWallet : public IWalletMessageConsumer
    {
        virtual void OnWalletMessage(const WalletID& peerID, const SetTxParameter&) override {};
    };

    /**
     *  Implementation of test network for SwapOffersBoard class.
     *  SwapOffersBoard uses BaseMessageEndpoint::SendAndSign() to push outgoing messages and
     *  FlyClient::INetwork for incoming messages.
     *  Main idea is to test real BaseMessageEndpoint::SendAndSign() implementation with board.
     */
    class MockNetwork : public BaseMessageEndpoint, public FlyClient::INetwork
    {
    public:
        MockNetwork(IWalletMessageConsumer& wallet, const IWalletDB::Ptr& walletDB, IPrivateKeyKeeper::Ptr keyKeeper)
            : BaseMessageEndpoint(wallet, walletDB, keyKeeper)
        {};

        // INetwork
        virtual void Connect() override {};
        virtual void Disconnect() override {};
        virtual void PostRequestInternal(FlyClient::Request&) override {};
        virtual void BbsSubscribe(BbsChannel channel, Timestamp ts, FlyClient::IBbsReceiver* callback) override
        {
            m_subscriptions[channel].push_back(make_pair(callback, ts));
        };

        // IWalletMessageEndpoint
        /**
         *  Redirects BBS messages to subscribers
         */
        virtual void SendEncryptedMessage(const WalletID& peerID, const ByteBuffer& msg) override
        {
            beam::BbsChannel channel;
            peerID.m_Channel.Export(channel);
            auto search = m_subscriptions.find(channel);
            if (search != m_subscriptions.end())
            {
                BbsMsg bbsMsg;
                bbsMsg.m_Channel = channel;
                bbsMsg.m_Message = msg;
                bbsMsg.m_TimePosted = getTimestamp();
                for (const auto& pair : m_subscriptions[channel])
                {
                    pair.first->OnMsg(std::move(bbsMsg));
                }
            }
        };

    private:
        map<BbsChannel, vector<pair<FlyClient::IBbsReceiver*, Timestamp>>> m_subscriptions;
    };

    TxID& stepTxID(TxID& id)
    {
        for (uint8_t& i : id)
        {
            if (i < 0xff)
            {
                ++i;
                break;
            }
        }
        return id;
    }

    void TestProtocol()
    {
        cout << endl << "Test protocol" << endl;
        MockWallet mockWalletWallet;
        auto senderWalletDB = createSenderWalletDB();
        auto keyKeeper = make_shared<LocalPrivateKeyKeeper>(senderWalletDB, senderWalletDB->get_MasterKdf());
        MockNetwork mockNetwork(mockWalletWallet, senderWalletDB, keyKeeper);

        SwapOffersBoard Alice(mockNetwork, mockNetwork);
        size_t countOffers = 0;
        WALLET_CHECK_NO_THROW(countOffers = Alice.getOffersList().size());
        WALLET_CHECK(countOffers == 0);
        
        {
            cout << "Empty message" << endl;
            BbsMsg m;
            m.m_Channel = BbsChannel(proto::Bbs::s_MaxChannels);
            m.m_TimePosted = getTimestamp();
            m.m_Message = ByteBuffer();
            WALLET_CHECK_NO_THROW(Alice.OnMsg(move(m)));
            WALLET_CHECK_NO_THROW(countOffers = Alice.getOffersList().size());
            WALLET_CHECK(countOffers == 0);
        }
        {
            cout << "Message header too short" << endl;
            BbsMsg m;
            m.m_Channel = BbsChannel(proto::Bbs::s_MaxChannels);
            m.m_TimePosted = getTimestamp();
            m.m_Message = ByteBuffer(beam::MsgHeader::SIZE - 2, 't');
            WALLET_CHECK_NO_THROW(Alice.OnMsg(move(m)));
            WALLET_CHECK_NO_THROW(countOffers = Alice.getOffersList().size());
            WALLET_CHECK(countOffers == 0);
        }
        {
            cout << "Message contain only header" << endl;
            BbsMsg m;
            m.m_Channel = BbsChannel(proto::Bbs::s_MaxChannels);
            m.m_TimePosted = getTimestamp();
            ByteBuffer data;
            data.reserve(MsgHeader::SIZE);
            MsgHeader header(0,0,1,0,0);            
            header.write(data.data());
            m.m_Message = data;
            WALLET_CHECK_NO_THROW(Alice.OnMsg(move(m)));
            WALLET_CHECK_NO_THROW(countOffers = Alice.getOffersList().size());
            WALLET_CHECK(countOffers == 0);
        }
        {
            cout << "Unsupported version" << endl;
            BbsMsg m;
            m.m_Channel = BbsChannel(proto::Bbs::s_MaxChannels);
            m.m_TimePosted = getTimestamp();
            ByteBuffer data;
            data.reserve(MsgHeader::SIZE);
            MsgHeader header(1,2,3,0,0);
            header.write(data.data());
            m.m_Message = data;
            WALLET_CHECK_NO_THROW(Alice.OnMsg(move(m)));
            WALLET_CHECK_NO_THROW(countOffers = Alice.getOffersList().size());
            WALLET_CHECK(countOffers == 0);
        }
        {
            cout << "Wrong length" << endl;
            BbsMsg m;
            m.m_Channel = BbsChannel(proto::Bbs::s_MaxChannels);
            m.m_TimePosted = getTimestamp();
            ByteBuffer data;
            data.reserve(MsgHeader::SIZE);
            MsgHeader header(0,0,1,0,5);
            header.write(data.data());
            m.m_Message = data;
            WALLET_CHECK_NO_THROW(Alice.OnMsg(move(m)));
            WALLET_CHECK_NO_THROW(countOffers = Alice.getOffersList().size());
            WALLET_CHECK(countOffers == 0);
        }
        {
            cout << "Wrong message type" << endl;
            BbsMsg m;
            m.m_Channel = BbsChannel(proto::Bbs::s_MaxChannels);
            m.m_TimePosted = getTimestamp();
            ByteBuffer data;
            data.reserve(MsgHeader::SIZE);
            MsgHeader header(0,0,1,5,0);
            header.write(data.data());
            m.m_Message = data;
            WALLET_CHECK_NO_THROW(Alice.OnMsg(move(m)));
            WALLET_CHECK_NO_THROW(countOffers = Alice.getOffersList().size());
            WALLET_CHECK(countOffers == 0);
        }
        {
            cout << "Wrong body length" << endl;
            BbsMsg m;
            m.m_Channel = BbsChannel(proto::Bbs::s_MaxChannels);
            m.m_TimePosted = getTimestamp();
            ByteBuffer data;
            uint32_t bodyLength = 6;
            data.reserve(MsgHeader::SIZE + bodyLength);
            MsgHeader header(0,0,1,0,bodyLength);
            header.write(data.data());
            m.m_Message = data;
            WALLET_CHECK_NO_THROW(Alice.OnMsg(move(m)));
            WALLET_CHECK_NO_THROW(countOffers = Alice.getOffersList().size());
            WALLET_CHECK(countOffers == 0);
        }

        cout << "Test end" << endl;
    }

    void TestSignature()
    {
        cout << endl << "Test board messages signature" << endl;
        MockWallet mockWalletWallet;
        auto senderWalletDB = createSenderWalletDB();
        auto keyKeeper = make_shared<LocalPrivateKeyKeeper>(senderWalletDB, senderWalletDB->get_MasterKdf());
        MockNetwork mockNetwork(mockWalletWallet, senderWalletDB, keyKeeper);

        SwapOffersBoard Alice(mockNetwork, mockNetwork);
        WALLET_CHECK(Alice.getOffersList().size() == 0);

        // Fill offer
        TxID txId = {{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}};
        WalletAddress wa = storage::createAddress(*senderWalletDB, keyKeeper);
        SwapOffer correctOffer(txId, SwapOfferStatus::Pending, wa.m_walletID, AtomicSwapCoin::Bitcoin);
        // mandatory parameters
        correctOffer.SetParameter(TxParameterID::AtomicSwapCoin, correctOffer.m_coin);
        correctOffer.SetParameter(TxParameterID::AtomicSwapIsBeamSide, true);
        correctOffer.SetParameter(TxParameterID::Amount, Amount(15000));
        correctOffer.SetParameter(TxParameterID::AtomicSwapAmount, Amount(85224));
        correctOffer.SetParameter(TxParameterID::MinHeight, Height(123));
        correctOffer.SetParameter(TxParameterID::PeerResponseTime, Height(15));

        auto kdf = senderWalletDB->get_MasterKdf();
        size_t count = 0;
        {
            std::cout << "Wrong signature" << endl;
            const ByteBuffer msg = toByteBuffer(SwapOfferToken(correctOffer));
            ECC::Scalar::Native sk;
            SwapOfferConfirmation confirmation;
            kdf->DeriveKey(sk, ECC::Key::ID(wa.m_OwnID, Key::Type::Bbs));
            PeerID generatedPk;
            proto::Sk2Pk(generatedPk, sk);
            confirmation.m_offerData = msg;
            confirmation.Sign(sk);

            ByteBuffer signature = toByteBuffer(confirmation.m_Signature);
            signature.data()[0] = 't';  // corrupt signature

            size_t bodySize = msg.size() + signature.size();
            assert(bodySize <= UINT32_MAX);
            MsgHeader header(0, 0, 1, 0, static_cast<uint32_t>(bodySize));

            ByteBuffer finalMessage(header.SIZE);
            header.write(finalMessage.data());
            finalMessage.reserve(header.size + header.SIZE);
            std::copy(std::begin(msg), std::end(msg), std::back_inserter(finalMessage));
            std::copy(std::begin(signature), std::end(signature), std::back_inserter(finalMessage));

            BbsMsg m;
            m.m_Channel = BbsChannel(proto::Bbs::s_MaxChannels);
            m.m_TimePosted = getTimestamp();
            m.m_Message = finalMessage;
            WALLET_CHECK_NO_THROW(Alice.OnMsg(move(m)));
            WALLET_CHECK_NO_THROW(count = Alice.getOffersList().size());
            WALLET_CHECK(count == 0);
        }
        {
            std::cout << "Wrong public key" << endl;
            WalletAddress newAddr = storage::createAddress(*senderWalletDB, keyKeeper);
            correctOffer.m_publisherId = newAddr.m_walletID;
            // changed public key to new random
            const ByteBuffer msg = toByteBuffer(SwapOfferToken(correctOffer));
            ECC::Scalar::Native sk;
            SwapOfferConfirmation confirmation;
            kdf->DeriveKey(sk, ECC::Key::ID(wa.m_OwnID, Key::Type::Bbs));
            PeerID generatedPk;
            proto::Sk2Pk(generatedPk, sk);
            confirmation.m_offerData = msg;
            confirmation.Sign(sk);

            ByteBuffer signature = toByteBuffer(confirmation.m_Signature);

            size_t bodySize = msg.size() + signature.size();
            assert(bodySize <= UINT32_MAX);
            MsgHeader header(0, 0, 1, 0, static_cast<uint32_t>(bodySize));

            ByteBuffer finalMessage(header.SIZE);
            header.write(finalMessage.data());
            finalMessage.reserve(header.size + header.SIZE);
            std::copy(std::begin(msg), std::end(msg), std::back_inserter(finalMessage));
            std::copy(std::begin(signature), std::end(signature), std::back_inserter(finalMessage));

            BbsMsg m;
            m.m_Channel = BbsChannel(proto::Bbs::s_MaxChannels);
            m.m_TimePosted = getTimestamp();
            m.m_Message = finalMessage;
            WALLET_CHECK_NO_THROW(Alice.OnMsg(move(m)));
            WALLET_CHECK_NO_THROW(count = Alice.getOffersList().size());
            WALLET_CHECK(count == 0);
        }
        {
            std::cout << "Normal offer dispatch" << endl;
            correctOffer.m_publisherId = wa.m_walletID;
            // changed public key back to correct
            const ByteBuffer msg = toByteBuffer(SwapOfferToken(correctOffer));
            ECC::Scalar::Native sk;
            SwapOfferConfirmation confirmation;
            kdf->DeriveKey(sk, ECC::Key::ID(wa.m_OwnID, Key::Type::Bbs));
            PeerID generatedPk;
            proto::Sk2Pk(generatedPk, sk);
            confirmation.m_offerData = msg;
            confirmation.Sign(sk);

            ByteBuffer signature = toByteBuffer(confirmation.m_Signature);

            size_t bodySize = msg.size() + signature.size();
            assert(bodySize <= UINT32_MAX);
            MsgHeader header(0, 0, 1, 0, static_cast<uint32_t>(bodySize));

            ByteBuffer finalMessage(header.SIZE);
            header.write(finalMessage.data());
            finalMessage.reserve(header.size + header.SIZE);
            std::copy(std::begin(msg), std::end(msg), std::back_inserter(finalMessage));
            std::copy(std::begin(signature), std::end(signature), std::back_inserter(finalMessage));

            BbsMsg m;
            m.m_Channel = BbsChannel(proto::Bbs::s_MaxChannels);
            m.m_TimePosted = getTimestamp();
            m.m_Message = finalMessage;
            WALLET_CHECK_NO_THROW(Alice.OnMsg(move(m)));
            WALLET_CHECK_NO_THROW(count = Alice.getOffersList().size());
            WALLET_CHECK(count == 1);
        }

        cout << "Test end" << endl;
    }

    void TestMandatoryParameters()
    {
        cout << endl << "Test mandatory parameters validation" << endl;
        MockWallet mockWalletWallet;
        auto senderWalletDB = createSenderWalletDB();
        auto keyKeeper = make_shared<LocalPrivateKeyKeeper>(senderWalletDB, senderWalletDB->get_MasterKdf());
        MockNetwork mockNetwork(mockWalletWallet, senderWalletDB, keyKeeper);

        SwapOffersBoard Alice(mockNetwork, mockNetwork);
        WALLET_CHECK(Alice.getOffersList().size() == 0);

        // Fill offer
        TxID txId = {{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}};
        WalletAddress wa = storage::createAddress(*senderWalletDB, keyKeeper);
        senderWalletDB->saveAddress(wa);
        WalletID senderId = wa.m_walletID;
        SwapOffer correctOffer(txId, SwapOfferStatus::Pending, senderId, AtomicSwapCoin::Bitcoin);
        // mandatory parameters
        correctOffer.SetParameter(TxParameterID::AtomicSwapCoin, correctOffer.m_coin);
        correctOffer.SetParameter(TxParameterID::AtomicSwapIsBeamSide, true);
        correctOffer.SetParameter(TxParameterID::Amount, Amount(15000));
        correctOffer.SetParameter(TxParameterID::AtomicSwapAmount, Amount(85224));
        correctOffer.SetParameter(TxParameterID::MinHeight, Height(123));
        correctOffer.SetParameter(TxParameterID::PeerResponseTime, Height(15));

        size_t offersCount = 0;
        size_t count = 0;
        {
            cout << "Mandatory parameters presence:" << endl;
            std::array<TxParameterID,6> mandatoryParams {
                TxParameterID::AtomicSwapCoin,
                TxParameterID::AtomicSwapIsBeamSide,
                TxParameterID::Amount,
                TxParameterID::AtomicSwapAmount,
                TxParameterID::MinHeight,
                TxParameterID::PeerResponseTime };

            for (auto parameter : mandatoryParams)
            {
                stepTxID(txId);
                SwapOffer o = correctOffer;
                o.m_txId = txId;
                cout << "\tparameter " << static_cast<uint32_t>(parameter) << endl;
                o.DeleteParameter(parameter);
                Alice.publishOffer(o);
                WALLET_CHECK_NO_THROW(count = Alice.getOffersList().size());
                WALLET_CHECK(count == offersCount);
            }
        }
        {
            cout << "AtomicSwapCoin validation" << endl;
            stepTxID(txId);
            SwapOffer o = correctOffer;
            o.m_txId = txId;
            o.m_coin = AtomicSwapCoin::Unknown;
            Alice.publishOffer(o);
            WALLET_CHECK_NO_THROW(count = Alice.getOffersList().size());
            WALLET_CHECK(count == offersCount);
        }
        {
            cout << "SwapOfferStatus validation" << endl;
            stepTxID(txId);
            SwapOffer o = correctOffer;
            o.m_txId = txId;
            o.m_status = static_cast<SwapOfferStatus>(static_cast<uint32_t>(SwapOfferStatus::Failed) + 1);
            Alice.publishOffer(o);
            WALLET_CHECK_NO_THROW(count = Alice.getOffersList().size());
            WALLET_CHECK(count == offersCount);
        }

        stepTxID(txId);
        SwapOffer o = correctOffer;
        o.m_txId = txId;
        Alice.publishOffer(o);
        WALLET_CHECK(Alice.getOffersList().size() == ++offersCount);

        cout << "Test end" << endl;
    }

    void TestCommunication()
    {
        cout << endl << "Test boards communication" << endl;
        MockWallet mockWalletWallet;
        auto senderWalletDB = createSenderWalletDB();
        auto keyKeeper = make_shared<LocalPrivateKeyKeeper>(senderWalletDB, senderWalletDB->get_MasterKdf());
        MockNetwork mockNetwork(mockWalletWallet, senderWalletDB, keyKeeper);

        SwapOffersBoard Alice(mockNetwork, mockNetwork);
        SwapOffersBoard Bob(mockNetwork, mockNetwork);
        SwapOffersBoard Cory(mockNetwork, mockNetwork);

        WALLET_CHECK(Alice.getOffersList().size() == 0);
        WALLET_CHECK(Bob.getOffersList().size() == 0);
        WALLET_CHECK(Cory.getOffersList().size() == 0);

        // Fill offer
        TxID txId = {{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}};
        WalletAddress wa = storage::createAddress(*senderWalletDB, keyKeeper);
        senderWalletDB->saveAddress(wa);
        WalletID senderId = wa.m_walletID;
        SwapOffer correctOffer(txId, SwapOfferStatus::Pending, senderId, AtomicSwapCoin::Bitcoin);
        // mandatory parameters
        correctOffer.SetParameter(TxParameterID::AtomicSwapCoin, correctOffer.m_coin);
        correctOffer.SetParameter(TxParameterID::AtomicSwapIsBeamSide, true);
        correctOffer.SetParameter(TxParameterID::Amount, Amount(15000));
        correctOffer.SetParameter(TxParameterID::AtomicSwapAmount, Amount(85224));
        correctOffer.SetParameter(TxParameterID::MinHeight, Height(123));
        correctOffer.SetParameter(TxParameterID::PeerResponseTime, Height(15));

        {
            uint32_t executionCount = 0;
            MockBoardObserver testObserver([&executionCount](ChangeAction action, const vector<SwapOffer>& offers) {
                        WALLET_CHECK(action == ChangeAction::Added);
                        WALLET_CHECK(offers.size() == 1);
                        executionCount++;
                    });
            Alice.Subscribe(&testObserver);
            Bob.Subscribe(&testObserver);
            Cory.Subscribe(&testObserver);
            
            cout << "Normal offers dispatch; subscribers notification" << endl;
            Alice.publishOffer(correctOffer);
            WALLET_CHECK(Alice.getOffersList().size() == 1);
            WALLET_CHECK(Bob.getOffersList().size() == 1);
            WALLET_CHECK(Cory.getOffersList().size() == 1);
            WALLET_CHECK(executionCount == 3);
            
            cout << "Same TxID check" << endl;
            SwapOffer o = correctOffer;
            o.m_coin = AtomicSwapCoin::Qtum;
            Cory.publishOffer(o);
            WALLET_CHECK(Alice.getOffersList().size() == 1);
            WALLET_CHECK(Bob.getOffersList().size() == 1);
            WALLET_CHECK(Cory.getOffersList().size() == 1);
            WALLET_CHECK(Alice.getOffersList().front().m_coin == AtomicSwapCoin::Bitcoin);
            WALLET_CHECK(executionCount == 3);

            cout << "Different TxID" << endl;
            o = correctOffer;
            o.m_txId = stepTxID(txId);
            o.m_coin = AtomicSwapCoin::Qtum;
            Cory.publishOffer(o);
            WALLET_CHECK(Alice.getOffersList().size() == 2);
            WALLET_CHECK(Bob.getOffersList().size() == 2);
            WALLET_CHECK(Cory.getOffersList().size() == 2);
            WALLET_CHECK(executionCount == 6);

            Alice.Unsubscribe(&testObserver);
            Bob.Unsubscribe(&testObserver);
            Cory.Unsubscribe(&testObserver);

            cout << "Unsubscribe" << endl;
            o = correctOffer;
            o.m_txId = stepTxID(txId);
            o.m_coin = AtomicSwapCoin::Litecoin;
            Bob.publishOffer(o);
            WALLET_CHECK(Alice.getOffersList().size() == 3);
            WALLET_CHECK(Bob.getOffersList().size() == 3);
            WALLET_CHECK(Cory.getOffersList().size() == 3);
            WALLET_CHECK(executionCount == 6);
        }
        
        {
            cout << "Observers notifications on non-active offers" << endl;
            {
                MockBoardObserver testObserver([](ChangeAction action, const vector<SwapOffer>& offers) {
                            WALLET_CHECK(false);
                        });
                Bob.Subscribe(&testObserver);

                cout << "Offer status:" << endl;
                std::array<SwapOfferStatus,5> nonActiveStatuses {
                    SwapOfferStatus::InProgress,
                    SwapOfferStatus::Completed,
                    SwapOfferStatus::Canceled,
                    SwapOfferStatus::Expired,
                    SwapOfferStatus::Failed };

                for (auto s : nonActiveStatuses)
                {
                    SwapOffer o = correctOffer;
                    o.m_txId = stepTxID(txId);
                    cout << "\tparameter " << static_cast<uint32_t>(s) << endl;
                    o.m_status = s;
                    Alice.publishOffer(o);
                    WALLET_CHECK(Bob.getOffersList().size() == 3);
                }
                Bob.Unsubscribe(&testObserver);
            }
            {
                cout << "Active offer status" << endl;
                uint32_t execCount = 0;
                MockBoardObserver testObserver([&execCount](ChangeAction action, const vector<SwapOffer>& offers) {
                            execCount++;
                        });
                Bob.Subscribe(&testObserver);                
                SwapOffer o = correctOffer;
                o.m_txId = stepTxID(txId);
                o.m_status = SwapOfferStatus::Pending;
                Alice.publishOffer(o);
                WALLET_CHECK(Bob.getOffersList().size() == 4);
                WALLET_CHECK(execCount == 1);
            }
        }
        // observers notifications. if expires/failed.. pushed to observers
        // walletDB notifications on onTransactionChanged, onSystemStateChanged
        // case when no offer exist on board. but transaction went to state InProgress and Expired later. (2-3 updates a line)

        // offers without TxParams validation
        // don't push offers without mandatory params to subscribers

        cout << "Test end" << endl;
    }
} // namespace

int main()
{
    cout << "SwapOffersBoard tests:" << endl;

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    TestProtocol();
    TestSignature();
    TestMandatoryParameters();
    TestCommunication();

    boost::filesystem::remove(SenderWalletDB);

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}
