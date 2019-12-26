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

#include <map>
#include <vector>

#include "wallet/core/wallet_network.h"
#include "wallet/core/common.h"

using namespace beam::wallet;
using namespace beam::proto;
using namespace beam;

namespace
{

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
        m_subscriptions[channel].push_back(std::make_pair(callback, ts));
    };

    // IWalletMessageEndpoint
    /**
     *  Redirects BBS messages to subscribers
     */
    virtual void SendRawMessage(const WalletID& peerID, const ByteBuffer& msg) override
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
    std::map<BbsChannel, std::vector<std::pair<FlyClient::IBbsReceiver*, Timestamp>>> m_subscriptions;
};

} // namespace
