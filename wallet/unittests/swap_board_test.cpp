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

    TxID generateTxID()
    {
        std::srand(static_cast<unsigned int>(std::time(nullptr)));
        TxID txId;
        for (uint8_t& i : txId)
        {
            i = std::rand() % 255;
        }
        return txId;
    }

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

    SwapOffer generateOffer(TxID& txID, SwapOfferStatus s, const WalletID& pubK, AtomicSwapCoin c)
    {
        std::srand(static_cast<unsigned int>(std::time(nullptr)));
        SwapOffer o(txID, s, pubK, c);
        // mandatory parameters
        o.SetParameter(TxParameterID::AtomicSwapCoin, o.m_coin);
        o.SetParameter(TxParameterID::AtomicSwapIsBeamSide, std::rand() % 2);
        o.SetParameter(TxParameterID::Amount, Amount(std::rand() % 10000));
        o.SetParameter(TxParameterID::AtomicSwapAmount, Amount(std::rand() % 1000));
        o.SetParameter(TxParameterID::MinHeight, Height(std::rand() % 1000));
        o.SetParameter(TxParameterID::PeerResponseTime, Height(std::rand() % 500));
        return o;
    }

    void TestProtocolCorruption()
    {
        cout << endl << "Test protocol corruption" << endl;

        MockWallet mockWalletWallet;
        auto senderWalletDB = createSenderWalletDB();
        auto keyKeeper = make_shared<LocalPrivateKeyKeeper>(senderWalletDB, senderWalletDB->get_MasterKdf());
        MockNetwork mockNetwork(mockWalletWallet, senderWalletDB, keyKeeper);

        SwapOffersBoard Alice(mockNetwork, mockNetwork);
        size_t countOffers = 0;
        WALLET_CHECK_NO_THROW(countOffers = Alice.getOffersList().size());
        WALLET_CHECK(countOffers == 0);
        
        {
            cout << "Case: empty message" << endl;
            BbsMsg m;
            m.m_Channel = BbsChannel(proto::Bbs::s_MaxChannels);
            m.m_TimePosted = getTimestamp();
            m.m_Message = ByteBuffer();
            WALLET_CHECK_NO_THROW(Alice.OnMsg(move(m)));
            WALLET_CHECK_NO_THROW(countOffers = Alice.getOffersList().size());
            WALLET_CHECK(countOffers == 0);
        }
        {
            cout << "Case: message header too short" << endl;
            BbsMsg m;
            m.m_Channel = BbsChannel(proto::Bbs::s_MaxChannels);
            m.m_TimePosted = getTimestamp();
            m.m_Message = ByteBuffer(beam::MsgHeader::SIZE - 2, 't');
            WALLET_CHECK_NO_THROW(Alice.OnMsg(move(m)));
            WALLET_CHECK_NO_THROW(countOffers = Alice.getOffersList().size());
            WALLET_CHECK(countOffers == 0);
        }
        {
            cout << "Case: message contain only header" << endl;
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
            cout << "Case: unsupported version" << endl;
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
            cout << "Case: wrong length" << endl;
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
            cout << "Case: wrong message type" << endl;
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
            cout << "Case: wrong body length" << endl;
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

        TxID txId = generateTxID();
        auto kdf = senderWalletDB->get_MasterKdf();
        size_t count = 0;

        {
            std::cout << "Case: invalid signature" << endl;
            WalletAddress wa = storage::createAddress(*senderWalletDB, keyKeeper);
            SwapOffer offer = generateOffer(stepTxID(txId), SwapOfferStatus::Pending, wa.m_walletID, AtomicSwapCoin::Bitcoin);
            const ByteBuffer msg = toByteBuffer(SwapOfferToken(offer));
            ECC::Scalar::Native sk;
            SwapOfferConfirmation confirmation;
            kdf->DeriveKey(sk, ECC::Key::ID(wa.m_OwnID, Key::Type::Bbs));
            PeerID generatedPk;
            proto::Sk2Pk(generatedPk, sk);
            confirmation.m_offerData = msg;
            confirmation.Sign(sk);

            ByteBuffer signature = toByteBuffer(confirmation.m_Signature);
            signature.front() += 1;  // corrupt signature

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
            std::cout << "Case: invalid public key" << endl;
            WalletAddress wa = storage::createAddress(*senderWalletDB, keyKeeper);
            SwapOffer offer = generateOffer(stepTxID(txId), SwapOfferStatus::Pending, wa.m_walletID, AtomicSwapCoin::Bitcoin);
            WalletAddress newAddr = storage::createAddress(*senderWalletDB, keyKeeper);
            offer.m_publisherId = newAddr.m_walletID;   // changed public key to new random
            const ByteBuffer msg = toByteBuffer(SwapOfferToken(offer));
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
            std::cout << "Case: correct message" << endl;
            WalletAddress wa = storage::createAddress(*senderWalletDB, keyKeeper);
            SwapOffer offer = generateOffer(stepTxID(txId), SwapOfferStatus::Pending, wa.m_walletID, AtomicSwapCoin::Bitcoin);
            const ByteBuffer msg = toByteBuffer(SwapOfferToken(offer));
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

        TxID txId = generateTxID();
        WalletAddress wa = storage::createAddress(*senderWalletDB, keyKeeper);
        senderWalletDB->saveAddress(wa);
        SwapOffer correctOffer = generateOffer(txId, SwapOfferStatus::Pending, wa.m_walletID, AtomicSwapCoin::Bitcoin);
        
        size_t offersCount = 0;
        size_t count = 0;
        {
            cout << "Case: mandatory parameters presence:" << endl;
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
                cout << "\tparameter code " << static_cast<uint32_t>(parameter) << endl;
                o.DeleteParameter(parameter);
                Alice.publishOffer(o);
                WALLET_CHECK_NO_THROW(count = Alice.getOffersList().size());
                WALLET_CHECK(count == offersCount);
            }
        }
        {
            cout << "Case: AtomicSwapCoin parameter validation" << endl;
            stepTxID(txId);
            SwapOffer o = correctOffer;
            o.m_txId = txId;
            o.m_coin = AtomicSwapCoin::Unknown;
            Alice.publishOffer(o);
            WALLET_CHECK_NO_THROW(count = Alice.getOffersList().size());
            WALLET_CHECK(count == offersCount);
        }
        {
            cout << "Case: SwapOfferStatus parameter validation" << endl;
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
        cout << endl << "Test boards communication and notification" << endl;

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

        TxID txId = generateTxID();
        WalletAddress wa = storage::createAddress(*senderWalletDB, keyKeeper);
        senderWalletDB->saveAddress(wa);
        SwapOffer correctOffer = generateOffer(txId, SwapOfferStatus::Pending, wa.m_walletID, AtomicSwapCoin::Bitcoin);
        
        size_t offersCount = 0;
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
            
            cout << "Case: normal dispatch and notification" << endl;
            SwapOffer o1 = correctOffer;
            SwapOffer o2 = correctOffer;
            SwapOffer o3 = correctOffer;
            o2.m_txId = stepTxID(txId);
            o3.m_txId = stepTxID(txId);
            Alice.publishOffer(o1);
            Bob.publishOffer(o2);
            Cory.publishOffer(o3);
            offersCount += 3;
            WALLET_CHECK(Alice.getOffersList().size() == offersCount);
            WALLET_CHECK(Bob.getOffersList().size() == offersCount);
            WALLET_CHECK(Cory.getOffersList().size() == offersCount);
            WALLET_CHECK(executionCount == 9);
            {
                auto receivedOffer = Bob.getOffersList().front();
                std::array<TxParameterID,6> paramsToCompare {
                    TxParameterID::AtomicSwapCoin,
                    TxParameterID::AtomicSwapIsBeamSide,
                    TxParameterID::Amount,
                    TxParameterID::AtomicSwapAmount,
                    TxParameterID::MinHeight,
                    TxParameterID::PeerResponseTime
                };
                for (auto p : paramsToCompare)
                {
                    auto receivedValue = receivedOffer.GetParameter(p);
                    auto dispatchedValue = correctOffer.GetParameter(p);
                    WALLET_CHECK(receivedValue && dispatchedValue);
                    WALLET_CHECK(*receivedValue == *dispatchedValue);
                }
            }
            
            cout << "Case: ignore same TxID" << endl;
            SwapOffer o4 = correctOffer;
            o4.m_coin = AtomicSwapCoin::Qtum;
            Cory.publishOffer(o4);
            WALLET_CHECK(Alice.getOffersList().size() == offersCount);
            WALLET_CHECK(Bob.getOffersList().size() == offersCount);
            WALLET_CHECK(Cory.getOffersList().size() == offersCount);
            WALLET_CHECK(Alice.getOffersList().front().m_coin == AtomicSwapCoin::Bitcoin);
            WALLET_CHECK(executionCount == 9);

            cout << "Case: different TxID" << endl;
            o4.m_txId = stepTxID(txId);
            o4.m_coin = AtomicSwapCoin::Qtum;
            Cory.publishOffer(o4);
            offersCount++;
            WALLET_CHECK(Alice.getOffersList().size() == offersCount);
            WALLET_CHECK(Bob.getOffersList().size() == offersCount);
            WALLET_CHECK(Cory.getOffersList().size() == offersCount);
            WALLET_CHECK(executionCount == 12);

            Alice.Unsubscribe(&testObserver);
            Bob.Unsubscribe(&testObserver);
            Cory.Unsubscribe(&testObserver);

            cout << "Case: unsubscribe stops notification" << endl;
            o4 = correctOffer;
            o4.m_txId = stepTxID(txId);
            o4.m_coin = AtomicSwapCoin::Litecoin;
            Bob.publishOffer(o4);
            offersCount++;
            WALLET_CHECK(Alice.getOffersList().size() == offersCount);
            WALLET_CHECK(Bob.getOffersList().size() == offersCount);
            WALLET_CHECK(Cory.getOffersList().size() == offersCount);
            WALLET_CHECK(executionCount == 12);
        }
        
        {
            uint32_t execCount = 0;
            MockBoardObserver testObserver([&execCount](ChangeAction action, const vector<SwapOffer>& offers) {
                    execCount++;
                });
            Bob.Subscribe(&testObserver);
            {
                cout << "Case: no notification on new offer in status:" << endl;
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
                    WALLET_CHECK(Bob.getOffersList().size() == offersCount);
                }
                WALLET_CHECK(execCount == 0);
            }
            {
                cout << "Case: notification on new offer in Pending status" << endl;
                SwapOffer o = correctOffer;
                o.m_txId = stepTxID(txId);
                o.m_status = SwapOfferStatus::Pending;
                Alice.publishOffer(o);
                offersCount++;
                WALLET_CHECK(Bob.getOffersList().size() == offersCount);
                WALLET_CHECK(execCount == 1);
            }
            Bob.Unsubscribe(&testObserver);
        }
        cout << "Test end" << endl;
    }

    void TestLinkedTransactionChanges()
    {
        cout << endl << "Test linked transaction status changes" << endl;

        MockWallet mockWalletWallet;
        auto senderWalletDB = createSenderWalletDB();
        auto keyKeeper = make_shared<LocalPrivateKeyKeeper>(senderWalletDB, senderWalletDB->get_MasterKdf());
        MockNetwork mockNetwork(mockWalletWallet, senderWalletDB, keyKeeper);

        SwapOffersBoard Alice(mockNetwork, mockNetwork);
        SwapOffersBoard Bob(mockNetwork, mockNetwork);

        TxID txId = generateTxID();
        WalletAddress wa = storage::createAddress(*senderWalletDB, keyKeeper);
        senderWalletDB->saveAddress(wa);
        SwapOffer correctOffer = generateOffer(txId, SwapOfferStatus::Pending, wa.m_walletID, AtomicSwapCoin::Bitcoin);

        size_t offerCount = 0;
        {
            cout << "Case: offers removed when Tx state changes to InProgress, Canceled, Failed" << endl;

            SwapOffer o1 = correctOffer;
            SwapOffer o2 = correctOffer;
            SwapOffer o3 = correctOffer;
            SwapOffer o4 = correctOffer;
            SwapOffer o5 = correctOffer;
            o1.m_txId = stepTxID(txId);
            o2.m_txId = stepTxID(txId);
            o3.m_txId = stepTxID(txId);
            o4.m_txId = stepTxID(txId);
            o5.m_txId = stepTxID(txId);
            Alice.publishOffer(o1);
            Alice.publishOffer(o2);
            Alice.publishOffer(o3);
            Alice.publishOffer(o4);
            Alice.publishOffer(o5);
            offerCount += 5;
            WALLET_CHECK(Bob.getOffersList().size() == offerCount);
            WALLET_CHECK(Alice.getOffersList().size() == offerCount);

            TxDescription tx1(o1.m_txId, TxType::AtomicSwap, Amount(852), Amount(741), Height(789));
            TxDescription tx2(o2.m_txId, TxType::AtomicSwap, Amount(852), Amount(741), Height(789));
            TxDescription tx3(o3.m_txId, TxType::AtomicSwap, Amount(852), Amount(741), Height(789));
            TxDescription tx4(o4.m_txId, TxType::AtomicSwap, Amount(852), Amount(741), Height(789));
            TxDescription tx5(o4.m_txId, TxType::AtomicSwap, Amount(852), Amount(741), Height(789));
            TxDescription tx6(o4.m_txId, TxType::AtomicSwap, Amount(852), Amount(741), Height(789));
            // this TxType is ignored
            TxDescription tx7(o4.m_txId, TxType::Simple, Amount(852), Amount(741), Height(789));
            tx7.m_status = wallet::TxStatus::InProgress;
            // these states have to deactivate offer
            tx1.m_status = wallet::TxStatus::InProgress;
            tx2.m_status = wallet::TxStatus::Canceled;
            tx3.m_status = wallet::TxStatus::Failed;
            // these are ignored
            tx4.m_status = wallet::TxStatus::Pending;
            tx5.m_status = wallet::TxStatus::Completed;
            tx6.m_status = wallet::TxStatus::Registering;
            uint32_t exCount = 0;
            MockBoardObserver obsRemove([&exCount](ChangeAction action, const vector<SwapOffer>& offers) {
                        WALLET_CHECK(action == ChangeAction::Removed);
                        exCount++;
                    });
            Bob.Subscribe(&obsRemove);
            Alice.onTransactionChanged(ChangeAction::Updated, {tx5, tx4, tx1, tx3, tx2, tx6, tx7});
            Bob.Unsubscribe(&obsRemove);
            offerCount -= 3;
            WALLET_CHECK(Bob.getOffersList().size() == offerCount);
            WALLET_CHECK(Alice.getOffersList().size() == offerCount);
            WALLET_CHECK(exCount == 3);

            // cancel the remaining offers
            tx4.m_txId = o4.m_txId;
            tx4.m_status = wallet::TxStatus::Canceled;
            tx4.m_txType = TxType::AtomicSwap;
            tx5.m_txId = o5.m_txId;
            tx5.m_status = wallet::TxStatus::Canceled;
            tx5.m_txType = TxType::AtomicSwap;
            Alice.onTransactionChanged(ChangeAction::Updated, {tx4, tx5});
            offerCount -= 2;
            WALLET_CHECK(Bob.getOffersList().size() == offerCount);
            WALLET_CHECK(Alice.getOffersList().size() == offerCount);
            WALLET_CHECK(offerCount == 0);
        }

        {
            cout << "Case: offers removed when chain height growns beyond expiration" << endl;

            SwapOffer aliceOffer = correctOffer;
            SwapOffer bobOffer = correctOffer;
            aliceOffer.m_txId = stepTxID(txId);
            bobOffer.m_txId = stepTxID(txId);
            Bob.publishOffer(bobOffer);
            Alice.publishOffer(aliceOffer);
            offerCount += 2;

            WALLET_CHECK(Alice.getOffersList().size() == offerCount);
            WALLET_CHECK(Bob.getOffersList().size() == offerCount);

            Block::SystemState::ID expiredHeight, nonExpiredHeight;
            auto h = aliceOffer.GetParameter<Height>(TxParameterID::MinHeight);
            auto t = aliceOffer.GetParameter<Height>(TxParameterID::PeerResponseTime);
            expiredHeight.m_Height = *h + *t;
            nonExpiredHeight.m_Height = *h + *t - Height(1);

            uint32_t exCount = 0;
            MockBoardObserver obsRemove([&exCount](ChangeAction action, const vector<SwapOffer>& offers) {
                WALLET_CHECK(action == ChangeAction::Removed);
                WALLET_CHECK(offers.front().m_status == SwapOfferStatus::Expired);
                exCount++;
            });

            Bob.Subscribe(&obsRemove);
            Bob.onSystemStateChanged(nonExpiredHeight);
            WALLET_CHECK(Alice.getOffersList().size() == offerCount);
            WALLET_CHECK(Bob.getOffersList().size() == offerCount);
            WALLET_CHECK(exCount == 0);
            Bob.Unsubscribe(&obsRemove);

            Alice.Subscribe(&obsRemove);
            Alice.onSystemStateChanged(expiredHeight);
            Alice.Unsubscribe(&obsRemove);
            WALLET_CHECK(Alice.getOffersList().size() == offerCount - 2);
            WALLET_CHECK(Bob.getOffersList().size() == offerCount);
            WALLET_CHECK(exCount == 2);
        }

        cout << "Test end" << endl;
    }

    void TestDelayedOfferUpdate()
    {
        cout << endl << "Test delayed offer update" << endl;

        MockWallet mockWalletWallet;
        auto senderWalletDB = createSenderWalletDB();
        auto keyKeeper = make_shared<LocalPrivateKeyKeeper>(senderWalletDB, senderWalletDB->get_MasterKdf());
        MockNetwork mockNetwork(mockWalletWallet, senderWalletDB, keyKeeper);

        SwapOffersBoard Alice(mockNetwork, mockNetwork);
        SwapOffersBoard Bob(mockNetwork, mockNetwork);

        TxID txId = generateTxID();
        WalletAddress wa = storage::createAddress(*senderWalletDB, keyKeeper);
        senderWalletDB->saveAddress(wa);
        SwapOffer correctOffer = generateOffer(txId, SwapOfferStatus::Pending, wa.m_walletID, AtomicSwapCoin::Bitcoin);

        uint32_t exCount = 0;
        MockBoardObserver observer([&exCount](ChangeAction action, const vector<SwapOffer>& offers) { exCount++; });
        {
            cout << "Case: delayed offer update broadcast to network" << endl;
            // Case when no offer exist on board.
            // Transaction steps to states InProgress and Expired or other.
            // Board doesn't know if offer exits in network and doesn't broadcast status update.
            // Offer appear on board. Offer status update has to be broadcasted.
            SwapOffer o = correctOffer;
            TxDescription tx(o.m_txId, TxType::AtomicSwap, Amount(951), Amount(753), Height(654));
            
            tx.m_status = wallet::TxStatus::InProgress;
            Alice.Subscribe(&observer);
            Alice.onTransactionChanged(ChangeAction::Updated, {tx});
            WALLET_CHECK(exCount == 0);
            WALLET_CHECK(Alice.getOffersList().size() == 0);
            WALLET_CHECK(Bob.getOffersList().size() == 0);

            tx.m_status = wallet::TxStatus::Failed;
            Alice.onTransactionChanged(ChangeAction::Updated, {tx});
            WALLET_CHECK(exCount == 0);
            WALLET_CHECK(Alice.getOffersList().size() == 0);
            WALLET_CHECK(Bob.getOffersList().size() == 0);
            
            tx.m_status = wallet::TxStatus::Canceled;
            Alice.onTransactionChanged(ChangeAction::Updated, {tx});
            WALLET_CHECK(exCount == 0);
            WALLET_CHECK(Alice.getOffersList().size() == 0);
            WALLET_CHECK(Bob.getOffersList().size() == 0);

            Bob.publishOffer(o);
            WALLET_CHECK(exCount == 0);
            WALLET_CHECK(Alice.getOffersList().size() == 0);
            WALLET_CHECK(Bob.getOffersList().size() == 0);
        }
        
        cout << "Test end" << endl;
    }

} // namespace

int main()
{
    cout << "SwapOffersBoard tests:" << endl;

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    TestProtocolCorruption();
    TestSignature();
    TestMandatoryParameters();
    TestCommunication();
    TestLinkedTransactionChanges();
    TestDelayedOfferUpdate();

    boost::filesystem::remove(SenderWalletDB);

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}
