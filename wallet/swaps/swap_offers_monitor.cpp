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

#include "swap_offers_monitor.h"

namespace beam::wallet
{
    SwapOffersMonitor::SwapOffersMonitor(FlyClient::INetwork& network, IWalletObserver& observer, IWalletMessageEndpoint& messageEndpoint)
        :   m_network(network),
            m_observer(observer),
            m_messageEndpoint(messageEndpoint)
    {
    }

    const std::map<AtomicSwapCoin, BbsChannel> SwapOffersMonitor::m_channelsMap =
    {
        {AtomicSwapCoin::Bitcoin, proto::Bbs::s_MaxChannels - 1},
        {AtomicSwapCoin::Litecoin, proto::Bbs::s_MaxChannels - 2},
        {AtomicSwapCoin::Qtum, proto::Bbs::s_MaxChannels - 3}
    };

    void SwapOffersMonitor::OnMsg(proto::BbsMsg &&msg)
    {
        if (msg.m_Message.empty())
            return;

        TxToken token;
        if (fromByteBuffer(msg.m_Message, token))
        {
            auto txParams = std::make_optional<TxParameters>(token.UnpackParameters());

            if (txParams.has_value())
            {
                auto offer = txParams.value();
                auto txId = offer.GetTxID();
                if (txId.has_value())
                {
                    m_offersCache[txId.value()] = offer;
                    m_observer.onSwapOffersChanged(ChangeAction::Added, std::vector<SwapOffer>{offer});
                }
            }

        }
        else
        {
            LOG_WARNING() << "not crypted BBS deserialization failed";
        }
    }

    void SwapOffersMonitor::listenChannel(AtomicSwapCoin coinType)
    {
        auto it = m_channelsMap.find(coinType);
        if (it != std::cend(m_channelsMap))
        {
            auto channel = it->second;
            if (m_activeChannel.has_value() && *m_activeChannel != channel)
            {
                // unsubscribe from active channel
                m_network.BbsSubscribe(*m_activeChannel, 0, nullptr);
                m_offersCache.clear();
            }
            // subscribe to new channel
            m_network.BbsSubscribe(channel, m_lastTimestamp, this);
            m_activeChannel = channel;
        }
        else
        {
            assert(false && "Unsupported coin type");
        }
        
    }

    auto SwapOffersMonitor::getOffersList() const -> std::vector<SwapOffer>
    {
        std::vector<SwapOffer> offers;

        for (auto offer : m_offersCache)
        {
            offers.push_back(offer.second);
        }

        return offers;
    }

    void SwapOffersMonitor::publishOffer(const SwapOffer& offer) const
    {
        // todo: choise channel from offer parameter
        if (!m_activeChannel.has_value())
            return;
        WalletID wId;
        wId.m_Channel = m_activeChannel.value();

        beam::wallet::TxToken token(offer);
        m_messageEndpoint.SendEncryptedMessage(wId, toByteBuffer(token));
    }

} // namespace beam::wallet
