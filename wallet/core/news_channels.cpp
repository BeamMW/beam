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
    NewsChannelsReader::NewsChannelsReader(FlyClient::INetwork& network)
        : m_network(network),
    {
        for (auto channel : m_channels)
        {
            m_network.BbsSubscribe(channel, m_lastTimestamp, this);
        }
    }

    const std::set<BbsChannel> NewsChannelsReader::m_channels =
    {
        proto::Bbs::s_MaxChannels + BbsChannelsOffset,
    };

    void NewsChannelsReader::OnMsg(proto::BbsMsg &&msg)
    {
        if (msg.m_Message.empty() || msg.m_Message.size() < MsgHeader::SIZE)
            return;

        NewsMessage news;
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
            d & news;
            d & confirmation.m_Signature;
        }
        catch(...)
        {
            LOG_WARNING() << "news message deserialization exception";
            return;
        }
        
        confirmation.m_data = toByteBuffer(news);
        
        for (const auto& pubKey : m_publicKeys)
        {
            if (confirmation.IsValid(pubKey))
            {
                // do smth
                notifySubscribers(/*news*/);
                break;
            }
        }
    }

    void NewsChannelsReader::Subscribe(INewsObserver* observer)
    {
        assert(std::find(m_subscribers.begin(), m_subscribers.end(), observer) == m_subscribers.end());

        m_subscribers.push_back(observer);
    }

    void NewsChannelsReader::Unsubscribe(INewsObserver* observer)
    {
        auto it = std::find(m_subscribers.begin(), m_subscribers.end(), observer);

        assert(it != m_subscribers.end());

        m_subscribers.erase(it);
    }

    void NewsChannelsReader::notifySubscribers() const
    {
        for (auto sub : m_subscribers)
        {
            sub->onNewsUpdate();
        }
    }
} // namespace beam::wallet
