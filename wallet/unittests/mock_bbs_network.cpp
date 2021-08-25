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
 *  Implementation of test BBS network.
 */
class MockBbsNetwork : public IWalletMessageEndpoint, public FlyClient::INetwork
{
public:
    MockBbsNetwork() = default;

    static std::shared_ptr<MockBbsNetwork> CreateInstance()
    {
        return std::make_shared<MockBbsNetwork>();
    }

    // INetwork
    virtual void Connect() override {};
    virtual void Disconnect() override {};
    virtual void PostRequestInternal(FlyClient::Request&) override {};
    virtual void BbsSubscribe(BbsChannel channel, Timestamp ts, FlyClient::IBbsReceiver* subscriber) override
    {
        m_subscriptions[channel].push_back(std::make_pair(subscriber, ts));
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
    virtual void Send(const WalletID& peerID, const SetTxParameter& msg) override {};

private:
    std::map<BbsChannel, std::vector<std::pair<FlyClient::IBbsReceiver*, Timestamp>>> m_subscriptions;
};

struct MockTimestampHolder : ITimestampHolder
{
    Timestamp GetTimestamp(BbsChannel channel) override
    {
        return 0;
    }
    void UpdateTimestamp(const proto::BbsMsg& msg) override
    {

    }

    static ITimestampHolder::Ptr CreateInstance()
    {
        return std::make_shared<MockTimestampHolder>();
    }
};
} // namespace
