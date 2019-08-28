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

#include "test_helpers.h"

#include <assert.h>
#include <iostream>
#include <map>

WALLET_TEST_INIT

#include "../swaps/swap_offers_board.h"
#include "../swaps/swap_offers_board.cpp"

using namespace beam;
using namespace beam::wallet;
using namespace std;

namespace
{
    struct Observer : public IWalletObserver
    {
        Observer(string name) :
            m_name(name) {};
        virtual void onSyncProgress(int done, int total) override {};
        virtual void onSwapOffersChanged(ChangeAction action, const vector<SwapOffer>& offers) override
        {
            cout << "Observer " << m_name << ": swap offer changed\n";
        }
        string m_name;
    };

    struct MockNetwork : public FlyClient::INetwork, IWalletMessageEndpoint
    {
        // INetwork
        virtual void Connect() override {};
        virtual void Disconnect() override {};
        virtual void PostRequestInternal(FlyClient::Request&) override {};
        virtual void BbsSubscribe(BbsChannel channel, Timestamp ts, FlyClient::IBbsReceiver* callback) override
        {
            subscriptions[channel] = make_pair(callback, ts);
        };

        // IWalletMessageEndpoint
        virtual void Send(const WalletID& peerID, const SetTxParameter& msg) override {};
        virtual void SendEncryptedMessage(const WalletID& peerID, const ByteBuffer& msg) override
        {
            BbsChannel channel;
            peerID.m_Channel.Export(channel);
            auto it = subscriptions.find(channel);
            if (it != subscriptions.cend())
            {
                auto callbackPtr = it->second.first;
                if (callbackPtr)
                {
                    proto::BbsMsg message;
                    message.m_Channel = channel;
                    message.m_Message = msg;
                    callbackPtr->OnMsg(move(message));
                }
            }
        };

        map<BbsChannel, pair<FlyClient::IBbsReceiver*, Timestamp>> subscriptions;
    };

    void TestSwapBoardsCommunication()
    {
        cout << "\nTesting swap offer boards communication with mock network...\n";

        Observer senderObserver("sender");
        Observer receiverObserver("receiver");

        MockNetwork mockNetwork;

        SwapOffersBoard senderBoard(mockNetwork, senderObserver, mockNetwork);
        SwapOffersBoard receiverBoard(mockNetwork, receiverObserver, mockNetwork);

        // test if only subscribed coin offer stored
        receiverBoard.subscribe(AtomicSwapCoin::Bitcoin);

        SwapOffer offer1(TxID{{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}});
        SwapOffer offer2(TxID{{10,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}});
        offer1.SetParameter(TxParameterID::AtomicSwapCoin, toByteBuffer(AtomicSwapCoin::Bitcoin));
        offer2.SetParameter(TxParameterID::AtomicSwapCoin, toByteBuffer(AtomicSwapCoin::Litecoin));
        senderBoard.publishOffer(offer1);
        senderBoard.publishOffer(offer2);
        auto offersList = receiverBoard.getOffersList();
        WALLET_CHECK(offersList.size() == 1);

        // test backward offers transmition
        senderBoard.subscribe(AtomicSwapCoin::Qtum);

        offer2.SetParameter(TxParameterID::AtomicSwapCoin, toByteBuffer(AtomicSwapCoin::Bitcoin));
        senderBoard.publishOffer(offer2);

        offersList = receiverBoard.getOffersList();
        WALLET_CHECK(offersList.size() == 2);
        offersList = senderBoard.getOffersList();
        WALLET_CHECK(offersList.size() == 0);

        offer1.SetParameter(TxParameterID::AtomicSwapCoin, toByteBuffer(AtomicSwapCoin::Qtum));
        offer2.SetParameter(TxParameterID::AtomicSwapCoin, toByteBuffer(AtomicSwapCoin::Qtum));
        receiverBoard.publishOffer(offer1);
        receiverBoard.publishOffer(offer2);
        offersList = senderBoard.getOffersList();
        WALLET_CHECK(offersList.size() == 2);

        // test offer corruption
        receiverBoard.subscribe(AtomicSwapCoin::Litecoin);
        offersList = receiverBoard.getOffersList();
        WALLET_CHECK(offersList.size() == 0);

        offer1.SetParameter(TxParameterID::AtomicSwapCoin, toByteBuffer(AtomicSwapCoin::Litecoin));
        senderBoard.publishOffer(offer1);
        offersList = receiverBoard.getOffersList();
        WALLET_CHECK(offersList.size() == 1);
        WALLET_CHECK(offersList.front() == offer1);
        
    }
} // namespace

int main()
{        
    TestSwapBoardsCommunication();        

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}
