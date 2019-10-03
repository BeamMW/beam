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

#include "swap_offers_board.h"

#include "p2p/protocol_base.h"

namespace beam::wallet
{
    SwapOffersBoard::SwapOffersBoard(FlyClient::INetwork& network, IWalletMessageEndpoint& messageEndpoint)
        :   m_network(network),
            m_messageEndpoint(messageEndpoint)
    {
    }

    const std::map<AtomicSwapCoin, BbsChannel> SwapOffersBoard::m_channelsMap =
    {
        {AtomicSwapCoin::Bitcoin, proto::Bbs::s_MaxChannels},
        {AtomicSwapCoin::Litecoin, proto::Bbs::s_MaxChannels + 1},
        {AtomicSwapCoin::Qtum, proto::Bbs::s_MaxChannels + 2}
    };

    void SwapOffersBoard::OnMsg(proto::BbsMsg &&msg)
    {
        if (msg.m_Message.empty() || msg.m_Message.size() < MsgHeader::SIZE)
            return;

        SwapOfferToken token;
        SwapOfferConfirmation confirmation;

        try
        {
            MsgHeader header(msg.m_Message.data());
            if (header.V0 != 0 ||
                header.V1 != 0 ||
                header.V2 != m_protocolVersion ||
                header.type != 0)
            {
                LOG_WARNING() << "offer board message version unsupported";
                return;
            }

            // message body
            Deserializer d;
            d.reset(msg.m_Message.data() + header.SIZE, header.size);
            d & token;
            d & confirmation.m_Signature;
        }
        catch(...)
        {
            LOG_WARNING() << "offer board message deserialization exception";
            return;
        }
        
        auto offer = token.Unpack();
        auto txId = offer.m_txId;
        auto status = offer.m_status;
        auto publisherId = offer.m_publisherId;

        confirmation.m_offerData = toByteBuffer(token);
                    
        if (!confirmation.IsValid(publisherId.m_Pk))
        {
            LOG_WARNING() << "offer board message signature is invalid";
            return;
        }

        auto offerExist = m_offersCache.find(txId);
        if (offerExist == m_offersCache.end())
        {
            // new offer
            m_offersCache[txId] = offer;
            for (auto sub : m_subscribers)
            {
                sub->onSwapOffersChanged(ChangeAction::Added, std::vector<SwapOffer>{offer});
            }
        }
        else
        {
            // existing offer update
            if (status != SwapOfferStatus::Pending)
            {
                m_offersCache[txId].m_status = status;
                for (auto sub : m_subscribers)
                {
                    sub->onSwapOffersChanged(ChangeAction::Removed, std::vector<SwapOffer>{offer});
                }
            }
        }
    }

    void SwapOffersBoard::onTransactionChanged(ChangeAction action, const std::vector<TxDescription>& items)
    {
        if (action != ChangeAction::Removed)
        {
            for (const auto& item : items)
            {
                switch (item.m_status)
                {
                    case TxStatus::InProgress:
                        updateOffer(item.m_txId, SwapOfferStatus::InProgress);
                        break;
                    case TxStatus::Failed:
                        updateOffer(item.m_txId, SwapOfferStatus::Failed);
                        break;
                    case TxStatus::Canceled:
                        updateOffer(item.m_txId, SwapOfferStatus::Cancelled);
                        break;
                    default:
                        assert(false && "Unexpected tx status");
                        break;
                }
            }
        }
    }

    void SwapOffersBoard::selectSwapCoin(AtomicSwapCoin coinType)
    {
        auto it = m_channelsMap.find(coinType);
        if (it != std::cend(m_channelsMap))
        {
            auto channel = it->second;
            if (m_activeChannel && *m_activeChannel != channel)
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

    auto SwapOffersBoard::getOffersList() const -> std::vector<SwapOffer>
    {
        std::vector<SwapOffer> offers;

        for (auto offer : m_offersCache)
        {
            auto status = offer.second.m_status;
            if (status == SwapOfferStatus::Pending)
            {
                offers.push_back(offer.second);
            }
        }

        return offers;
    }

    auto SwapOffersBoard::getChannel(const SwapOffer& offer) const -> boost::optional<BbsChannel>
    {
        auto coinType = offer.GetParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);
        if (coinType)
        {
            auto it = m_channelsMap.find(*coinType);
            if (it != std::cend(m_channelsMap))
            {
                return it->second;
            }
        }
        return boost::none;
    }

    void SwapOffersBoard::publishOffer(const SwapOffer& offer) const
    {
        auto channel = getChannel(offer);
        
        if (channel)
        {
            LOG_INFO() << offer.m_txId << " Publish offer";
            beam::wallet::SwapOfferToken token(offer);
            m_messageEndpoint.SendAndSign(toByteBuffer(token), *channel, offer.m_publisherId, m_protocolVersion);
        }
    }

    void SwapOffersBoard::updateOffer(const TxID& offerTxID, SwapOfferStatus newStatus) const
    {
        auto offerExist = m_offersCache.find(offerTxID);
        if (offerExist != m_offersCache.end())
        {
            auto channel = getChannel(offerExist->second);
            auto publisherId = offerExist->second.m_publisherId;
            auto currentStatus = offerExist->second.m_status;

            if (channel && currentStatus != newStatus)
            {
                SwapOffer offerUpdate(offerTxID, newStatus, publisherId);

                LOG_INFO() << offerUpdate.m_txId << " Update offer status " << 
                                static_cast<uint32_t>(currentStatus) << " to " <<
                                static_cast<uint32_t>(newStatus);
                beam::wallet::SwapOfferToken token(offerUpdate);
                m_messageEndpoint.SendAndSign(toByteBuffer(token), *channel, publisherId, m_protocolVersion);
            }
        }
    }
    
    void SwapOffersBoard::Subscribe(ISwapOffersObserver* observer)
    {
        assert(std::find(m_subscribers.begin(), m_subscribers.end(), observer) == m_subscribers.end());

        m_subscribers.push_back(observer);
    }

    void SwapOffersBoard::Unsubscribe(ISwapOffersObserver* observer)
    {
        auto it = std::find(m_subscribers.begin(), m_subscribers.end(), observer);

        assert(it != m_subscribers.end());

        m_subscribers.erase(it);
    }

} // namespace beam::wallet
