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

#include "news_channels.h"

namespace beam::wallet
{
    NewsEndpoint::NewsEndpoint(FlyClient::INetwork& network)
        : m_network(network)
    {
        for (auto channel : m_channels)
        {
            m_network.BbsSubscribe(channel, m_lastTimestamp, this);
        }
    }

    const std::set<BbsChannel> NewsEndpoint::m_channels =
    {
        proto::Bbs::s_MaxChannels + BbsChannelsOffset,
    };

    void NewsEndpoint::OnMsg(proto::BbsMsg &&msg)
    {
        if (msg.m_Message.empty() || msg.m_Message.size() < MsgHeader::SIZE)
            return;

        NewsMessage freshNews;
        SignatureConfirmation confirmation;
        try
        {
            MsgHeader header(msg.m_Message.data());
            if (header.V0 != 0 ||
                header.V1 != 0 ||
                header.V2 != m_protocolVersion ||
                header.type != MsgType)
            {
                LOG_WARNING() << "news message version unsupported";
                return;
            }

            // message body
            Deserializer d;
            d.reset(msg.m_Message.data() + header.SIZE, header.size);
            d & freshNews;
            d & confirmation.m_Signature;
        }
        catch(...)
        {
            LOG_WARNING() << "news message deserialization exception";
            return;
        }
        
        confirmation.m_data = toByteBuffer(freshNews);
        
        auto it = std::find_if( std::cbegin(m_publicKeys),
                                std::cend(m_publicKeys),
                                [&confirmation](PeerID pk)
                                {
                                    return confirmation.IsValid(pk);
                                });
        if (it != m_publicKeys.cend())
        {
            // TODO polymorphic parsing
            notifySubscribers(freshNews);
        }
    }

    void NewsEndpoint::Subscribe(INewsObserver* observer)
    {
        assert(std::find(m_subscribers.begin(), m_subscribers.end(), observer) == m_subscribers.end());

        m_subscribers.push_back(observer);
    }

    void NewsEndpoint::Unsubscribe(INewsObserver* observer)
    {
        auto it = std::find(m_subscribers.begin(), m_subscribers.end(), observer);

        assert(it != m_subscribers.end());

        m_subscribers.erase(it);
    }

    void NewsEndpoint::notifySubscribers(NewsMessage msg) const
    {
        for (auto sub : m_subscribers)
        {
            sub->onNewsUpdate(msg);
        }
    }

    void NewsEndpoint::setPublicKeys(std::vector<PeerID> keys)
    {
        m_publicKeys.clear();
        for (const auto& key : keys)
        {
            m_publicKeys.push_back(key);
        }
    }

} // namespace beam::wallet
