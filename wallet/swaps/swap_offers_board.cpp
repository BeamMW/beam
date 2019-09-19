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

namespace beam::wallet
{
    SwapOffersBoard::SwapOffersBoard(FlyClient::INetwork& network, IWalletObserver& observer, IWalletMessageEndpoint& messageEndpoint)
        :   m_network(network),
            m_observer(observer),
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
        if (msg.m_Message.empty())
            return;

        // TODO: Offers validation
        TxToken token;
        SwapOfferConfirmation confirmation;

        Deserializer d;
        d.reset(msg.m_Message.data(), msg.m_Message.size());
        d & token;
        d & confirmation.m_Signature;
        
        auto offer = token.UnpackParameters();
        auto txId = offer.GetTxID();
        auto status = offer.GetParameter<TxStatus>(TxParameterID::Status);
        auto peerId = offer.GetParameter<WalletID>(TxParameterID::PeerID);
        if (txId && status && peerId)
        {
            confirmation.m_offerData = toByteBuffer(token);
                        
            if (!confirmation.IsValid(peerId->m_Pk))
            {
                LOG_WARNING() << "not crypted BBS signature is invalid";
                return;
            }

            auto offerExist = m_offersCache.find(*txId);
            if (offerExist == m_offersCache.end())
            {
                // new offer
                m_offersCache[*txId] = offer;
                m_observer.onSwapOffersChanged(ChangeAction::Added, std::vector<SwapOffer>{offer});
            }
            else
            {
                // existing offer update
                TxStatus newOfferStatus;
                if (!offer.GetParameter(TxParameterID::Status, newOfferStatus))
                {
                    return;
                }

                if (newOfferStatus != TxStatus::Pending)
                {
                    m_offersCache[*txId].SetParameter(TxParameterID::Status, newOfferStatus);
                    m_observer.onSwapOffersChanged(ChangeAction::Removed, std::vector<SwapOffer>{offer});
                }
            }
        }
    }

    void SwapOffersBoard::subscribe(AtomicSwapCoin coinType)
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
            auto status = offer.second.GetParameter<TxStatus>(TxParameterID::Status);
            if (status && (status.value() == TxStatus::Pending))
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
        auto peerId = offer.GetParameter<WalletID>(TxParameterID::PeerID);
        
        if (channel && peerId)
        {
            beam::wallet::TxToken token(offer);
            m_messageEndpoint.SendAndSign(toByteBuffer(token), channel.value(), peerId.value());
        }
    }

    void SwapOffersBoard::updateOffer(const TxID& offerTxID, TxStatus newStatus) const
    {
        auto offerExist = m_offersCache.find(offerTxID);
        if (offerExist != m_offersCache.end())
        {
            auto channel = getChannel(offerExist->second);
            auto peerId = offerExist->second.GetParameter<WalletID>(TxParameterID::PeerID);

            if (peerId && channel)
            {
                SwapOffer offerUpdate(offerTxID);
                offerUpdate.SetParameter(TxParameterID::Status, newStatus);
                offerUpdate.SetParameter(TxParameterID::PeerID, peerId.value());

                beam::wallet::TxToken token(offerUpdate);
                m_messageEndpoint.SendAndSign(toByteBuffer(token), channel.value(), peerId.value());
            }
        }
    }

} // namespace beam::wallet
