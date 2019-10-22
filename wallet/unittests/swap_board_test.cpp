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
        MockBoardObserver(string name, function<void(ChangeAction, const vector<SwapOffer>&)> checker) :
            m_name(name),
            m_testChecker(checker) {};

        virtual void onSwapOffersChanged(ChangeAction action, const vector<SwapOffer>& offers) override
        {
            cout << "Observer " << m_name << ": swap offer changed\n";
            m_testChecker(action, offers);
        }

        string m_name;
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

    void TestSwapOfferBoardProtocol()
    {
        cout << endl << "Test protocol." << endl;
        MockWallet mockWalletWallet;
        auto senderWalletDB = createSenderWalletDB();
        auto keyKeeper = make_shared<LocalPrivateKeyKeeper>(senderWalletDB, senderWalletDB->get_MasterKdf());
        MockNetwork mockNetwork(mockWalletWallet, senderWalletDB, keyKeeper);

        SwapOffersBoard Alice(mockNetwork, mockNetwork);
        size_t countOffers = 0;
        WALLET_CHECK_NO_THROW(countOffers = Alice.getOffersList().size());
        WALLET_CHECK(countOffers == 0);
        
        {
            cout << "Empty message." << endl;
            BbsMsg m;
            m.m_Channel = BbsChannel(proto::Bbs::s_MaxChannels);
            m.m_TimePosted = getTimestamp();
            m.m_Message = ByteBuffer();
            Alice.OnMsg(move(m));
            WALLET_CHECK_NO_THROW(countOffers = Alice.getOffersList().size());
            WALLET_CHECK(countOffers == 0);
        }
        {
            cout << "Message header too short." << endl;
            BbsMsg m;
            m.m_Channel = BbsChannel(proto::Bbs::s_MaxChannels);
            m.m_TimePosted = getTimestamp();
            m.m_Message = ByteBuffer(beam::MsgHeader::SIZE - 2, 't');
            Alice.OnMsg(move(m));
            WALLET_CHECK_NO_THROW(countOffers = Alice.getOffersList().size());
            WALLET_CHECK(countOffers == 0);
        }
        {
            cout << "Message contain only header." << endl;
            BbsMsg m;
            m.m_Channel = BbsChannel(proto::Bbs::s_MaxChannels);
            m.m_TimePosted = getTimestamp();
            ByteBuffer data;
            data.reserve(MsgHeader::SIZE);
            MsgHeader header(0,0,1,0,0);            
            header.write(data.data());
            m.m_Message = data;
            Alice.OnMsg(move(m));
            WALLET_CHECK_NO_THROW(countOffers = Alice.getOffersList().size());
            WALLET_CHECK(countOffers == 0);
        }
        {
            cout << "Unsupported version." << endl;
            BbsMsg m;
            m.m_Channel = BbsChannel(proto::Bbs::s_MaxChannels);
            m.m_TimePosted = getTimestamp();
            ByteBuffer data;
            data.reserve(MsgHeader::SIZE);
            MsgHeader header(1,2,3,0,0);
            header.write(data.data());
            m.m_Message = data;
            Alice.OnMsg(move(m));
            WALLET_CHECK_NO_THROW(countOffers = Alice.getOffersList().size());
            WALLET_CHECK(countOffers == 0);
        }
        {
            cout << "Wrong length." << endl;
            BbsMsg m;
            m.m_Channel = BbsChannel(proto::Bbs::s_MaxChannels);
            m.m_TimePosted = getTimestamp();
            ByteBuffer data;
            data.reserve(MsgHeader::SIZE);
            MsgHeader header(0,0,1,0,5);
            header.write(data.data());
            m.m_Message = data;
            Alice.OnMsg(move(m));
            WALLET_CHECK_NO_THROW(countOffers = Alice.getOffersList().size());
            WALLET_CHECK(countOffers == 0);
        }
        {
            cout << "Wrong message type." << endl;
            BbsMsg m;
            m.m_Channel = BbsChannel(proto::Bbs::s_MaxChannels);
            m.m_TimePosted = getTimestamp();
            ByteBuffer data;
            data.reserve(MsgHeader::SIZE);
            MsgHeader header(0,0,1,5,0);
            header.write(data.data());
            m.m_Message = data;
            Alice.OnMsg(move(m));
            WALLET_CHECK_NO_THROW(countOffers = Alice.getOffersList().size());
            WALLET_CHECK(countOffers == 0);
        }

        // wrong body and signature length
        // wrong signature
        // wrong body (offer structure)

        cout << "Test end" << endl;
    }

    void TestSwapOfferBoardCommunication()
    {
        cout << endl << "Test boards communication" << endl;
        MockWallet mockWalletWallet;
        auto senderWalletDB = createSenderWalletDB();
        auto keyKeeper = make_shared<LocalPrivateKeyKeeper>(senderWalletDB, senderWalletDB->get_MasterKdf());
        MockNetwork mockNetwork(mockWalletWallet, senderWalletDB, keyKeeper);

        SwapOffersBoard Alice(mockNetwork, mockNetwork);
        SwapOffersBoard Bob(mockNetwork, mockNetwork);
        // SwapOffersBoard Cory(mockNetwork, mockNetwork);

        // MockBoardObserver aliceObserver("Alice observer", [](ChangeAction a, const vector<SwapOffer>& o) {
        //             cout << "Action: " << static_cast<uint32_t>(a) << endl;
        //             cout << "Offers.size: " << o.size() << endl;
        //         });
        // Alice.Subscribe(&aliceObserver);

        WALLET_CHECK(Alice.getOffersList().size() == 0);
        WALLET_CHECK(Bob.getOffersList().size() == 0);

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

        cout << "Normal offer dispatch: " << endl;
        Alice.publishOffer(correctOffer);
        WALLET_CHECK(Alice.getOffersList().size() == 1);
        WALLET_CHECK(Bob.getOffersList().size() == 1);
        
        cout << "Mandatory parameters validation:" << endl;
        {
            std::array<TxParameterID,6> mandatoryParams {
                TxParameterID::AtomicSwapCoin,
                TxParameterID::AtomicSwapIsBeamSide,
                TxParameterID::Amount,
                TxParameterID::AtomicSwapAmount,
                TxParameterID::MinHeight,
                TxParameterID::PeerResponseTime };
            uint32_t offersCount = 1;

            for (auto parameter : mandatoryParams)
            {
                stepTxID(txId);
                SwapOffer o = correctOffer;
                o.m_txId = txId;
                cout << "\tparameter " << static_cast<uint32_t>(parameter) << endl;
                o.DeleteParameter(parameter);
                Alice.publishOffer(o);
                // No any new offer must pass validation
                WALLET_CHECK(Alice.getOffersList().size() == offersCount);
                WALLET_CHECK(Bob.getOffersList().size() == offersCount);
            }            
        }

        // ++offersCount;

        // validation
        // observers notifications. if expires/failed.. pushed to observers
        // walletDB notifications on onTransactionChanged, onSystemStateChanged
        // case when no offer exist on board. but transaction went to state InProgress and Expired later.

        cout << "Test end" << endl;
    }
} // namespace

int main()
{
    cout << "SwapOffersBoard tests:" << endl;

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    TestSwapOfferBoardProtocol();
    TestSwapOfferBoardCommunication();

    boost::filesystem::remove(SenderWalletDB);

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}
