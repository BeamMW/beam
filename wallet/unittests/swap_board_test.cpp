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
    // struct Observer : public ISwapOffersObserver
    // {
    //     Observer(string name) :
    //         m_name(name) {};
    //     virtual void onSwapOffersChanged(ChangeAction action, const vector<SwapOffer>& offers) override
    //     {
    //         cout << "Observer " << m_name << ": swap offer changed\n";
    //     }
    //     string m_name;
    // };

    /**
     *  Wallet implementation isn't used in this test.
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
        {
            cout << "\nMock network for SwapOffersBoard created" << endl;
        };

        // INetwork
        virtual void Connect() override {};
        virtual void Disconnect() override {};
        virtual void PostRequestInternal(FlyClient::Request&) override {};
        virtual void BbsSubscribe(BbsChannel channel, Timestamp ts, FlyClient::IBbsReceiver* callback)
        {
            m_subscriptions[channel] = make_pair(callback, ts);
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
                m_subscriptions[channel].first->OnMsg(std::move(bbsMsg));
            }
        };



    private:
        map<BbsChannel, pair<FlyClient::IBbsReceiver*, Timestamp>> m_subscriptions;
    };

    void TestSwapOfferBoardCominication()
    {
        cout << "TestSwapOfferBoardCominication" << endl;
        MockWallet mockWalletWallet;
        auto testWalletDB = createSenderWalletDB();
        auto keyKeeper = make_shared<LocalPrivateKeyKeeper>(testWalletDB, testWalletDB->get_MasterKdf());
        MockNetwork mockNetwork(mockWalletWallet, testWalletDB, keyKeeper);

        SwapOffersBoard Alice(mockNetwork, mockNetwork);
        SwapOffersBoard Bob(mockNetwork, mockNetwork);
        SwapOffersBoard Cory(mockNetwork, mockNetwork);

        auto offers = Alice.getOffersList();
        cout << "Empty board contains: " << offers.size() << " offers" << endl;

        cout << "TestSwapOfferBoardCominication end" << endl;
    }

    // void TestSwapBoardsCommunication()
    // {
    //     cout << "\nTesting swap offer boards communication with mock network...\n";

    //     Observer senderObserver("sender");
    //     Observer receiverObserver("receiver");

    //     MockNetwork mockNetwork;

    //     SwapOffersBoard senderBoard(mockNetwork, mockNetwork);
    //     SwapOffersBoard receiverBoard(mockNetwork, mockNetwork);

    //     // test if only subscribed coin offer stored
    //     receiverBoard.selectSwapCoin(AtomicSwapCoin::Bitcoin);

    //     // use CreateSwapParameters
    //     SwapOffer offer1(TxID{{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}});
    //     SwapOffer offer2(TxID{{10,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}});
    //     offer1.SetParameter(TxParameterID::AtomicSwapCoin, toByteBuffer(AtomicSwapCoin::Bitcoin));
    //     offer1.SetParameter(TxParameterID::Status, beam::wallet::TxStatus::Pending);
    //     offer2.SetParameter(TxParameterID::AtomicSwapCoin, toByteBuffer(AtomicSwapCoin::Litecoin));
    //     offer2.SetParameter(TxParameterID::Status, beam::wallet::TxStatus::Pending);
    //     senderBoard.publishOffer(offer1);
    //     senderBoard.publishOffer(offer2);
    //     auto offersList = receiverBoard.getOffersList();
    //     WALLET_CHECK(offersList.size() == 1);

    //     // test backward offers transmition
    //     senderBoard.selectSwapCoin(AtomicSwapCoin::Qtum);

    //     offer2.SetParameter(TxParameterID::AtomicSwapCoin, toByteBuffer(AtomicSwapCoin::Bitcoin));
    //     senderBoard.publishOffer(offer2);

    //     offersList = receiverBoard.getOffersList();
    //     WALLET_CHECK(offersList.size() == 2);
    //     offersList = senderBoard.getOffersList();
    //     WALLET_CHECK(offersList.size() == 0);

    //     offer1.SetParameter(TxParameterID::AtomicSwapCoin, toByteBuffer(AtomicSwapCoin::Qtum));
    //     offer2.SetParameter(TxParameterID::AtomicSwapCoin, toByteBuffer(AtomicSwapCoin::Qtum));
    //     receiverBoard.publishOffer(offer1);
    //     receiverBoard.publishOffer(offer2);
    //     offersList = senderBoard.getOffersList();
    //     WALLET_CHECK(offersList.size() == 2);

    //     // test offer corruption
    //     receiverBoard.selectSwapCoin(AtomicSwapCoin::Litecoin);
    //     offersList = receiverBoard.getOffersList();
    //     WALLET_CHECK(offersList.size() == 0);

    //     offer1.SetParameter(TxParameterID::AtomicSwapCoin, toByteBuffer(AtomicSwapCoin::Litecoin));
    //     senderBoard.publishOffer(offer1);
    //     offersList = receiverBoard.getOffersList();
    //     WALLET_CHECK(offersList.size() == 1);
    //     WALLET_CHECK(offersList.front() == offer1);
        
    // }
} // namespace

int main()
{
    cout << "Swap Offer Board tests:" << endl;

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    TestSwapOfferBoardCominication();

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}
