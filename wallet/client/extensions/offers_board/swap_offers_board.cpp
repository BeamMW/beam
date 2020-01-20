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
    /**
     *  @network            incoming bbs source
     *  @messageEndpoint    outgoing bbs destination
     *  @protocolHandler    offer board protocol handler
     */
    SwapOffersBoard::SwapOffersBoard(FlyClient::INetwork& network,
                                     IWalletMessageEndpoint& messageEndpoint,
                                     OfferBoardProtocolHandler& protocolHandler)
        :   m_network(network),
            m_messageEndpoint(messageEndpoint),
            m_protocolHandler(protocolHandler)
    {
        for (auto channel : m_channelsMap)
        {
            m_network.BbsSubscribe(channel.second, m_lastTimestamp, this);
        }
    }

    const std::map<AtomicSwapCoin, BbsChannel> SwapOffersBoard::m_channelsMap =
    {
        {AtomicSwapCoin::Bitcoin, proto::Bbs::s_MaxWalletChannels},
        {AtomicSwapCoin::Litecoin, proto::Bbs::s_MaxWalletChannels + 1},
        {AtomicSwapCoin::Qtum, proto::Bbs::s_MaxWalletChannels + 2}
    };

    void SwapOffersBoard::OnMsg(proto::BbsMsg &&msg)
    {
        auto newOffer = m_protocolHandler.parseMessage(msg.m_Message);
        if (!newOffer) return;

        if (newOffer->m_coin >= AtomicSwapCoin::Unknown || newOffer->m_status > SwapOfferStatus::Failed)
        {
            LOG_WARNING() << "offer board message is invalid";
            return;
        }

        auto it = m_offersCache.find(newOffer->m_txId);
        // New offer
        if (it == m_offersCache.end())
        {
            if (isOfferExpired(*newOffer) && newOffer->m_status == SwapOfferStatus::Pending)
            {
                newOffer->m_status = SwapOfferStatus::Expired;
            }
            
            m_offersCache[newOffer->m_txId] = *newOffer;

            if (newOffer->m_status == SwapOfferStatus::Pending)
            {
                notifySubscribers(ChangeAction::Added, std::vector<SwapOffer>{*newOffer});
            }
            else
            {
                // Don't push irrelevant offers to subscribers
            }
        }
        // Existing offer update
        else    
        {
            SwapOfferStatus existingStatus = m_offersCache[newOffer->m_txId].m_status;

            // Normal case
            if (existingStatus == SwapOfferStatus::Pending)
            {
                if (newOffer->m_status != SwapOfferStatus::Pending)
                {
                    m_offersCache[newOffer->m_txId].m_status = newOffer->m_status;
                    notifySubscribers(ChangeAction::Removed, std::vector<SwapOffer>{*newOffer});
                }
            }
            // Transaction state has changed asynchronously while board was offline.
            // Incomplete offer with SwapOfferStatus!=Pending was created.
            // If offer with SwapOfferStatus::Pending is still exist in network,
            // it need to be updated to latest status.
            else
            {
                if (newOffer->m_status == SwapOfferStatus::Pending)
                {
                    sendUpdateToNetwork(newOffer->m_txId, newOffer->m_publisherId, newOffer->m_coin, existingStatus);
                }
            }
        }
    }

    /**
     *  Watches for system state to remove stuck expired offers from board.
     *  Doesn't push any updates to network, just notify subscribers.
     */
    void SwapOffersBoard::onSystemStateChanged(const Block::SystemState::ID& stateID)
    {
        m_currentHeight = stateID.m_Height;

        for (auto& pair : m_offersCache)
        {
            auto& offer = pair.second;
            if (offer.m_status != SwapOfferStatus::Pending) continue;    // have to be already removed from board
            if (isOfferExpired(offer))
            {
                offer.m_status = SwapOfferStatus::Expired;
                notifySubscribers(ChangeAction::Removed, std::vector<SwapOffer>{offer});
            }
        }
    }

    /**
     *  Offers without PeerResponseTime or MinHeight
     *  are supposed to be invalid and expired by default.
     */
    bool SwapOffersBoard::isOfferExpired(const SwapOffer& offer) const
    {
        auto peerResponseTime = offer.GetParameter<Height>(TxParameterID::PeerResponseTime);
        auto minHeight = offer.GetParameter<Height>(TxParameterID::MinHeight);
            if (peerResponseTime && minHeight)
            {
                auto expiresHeight = *minHeight + *peerResponseTime;
            return expiresHeight <= m_currentHeight;
        }
        else return true;
    }

    void SwapOffersBoard::onTransactionChanged(ChangeAction action, const std::vector<TxDescription>& items)
    {
        if (action != ChangeAction::Removed)
        {
            for (const auto& item : items)
            {
                if (item.m_txType != TxType::AtomicSwap) continue;

                switch (item.m_status)
                {
                    case TxStatus::InProgress:
                        updateOffer(item.m_txId, SwapOfferStatus::InProgress);
                        break;
                    case TxStatus::Failed:
                    {
                        auto reason = item.GetParameter<TxFailureReason>(TxParameterID::InternalFailureReason);
                        SwapOfferStatus status = SwapOfferStatus::Failed;

                        if (reason && *reason == TxFailureReason::TransactionExpired)
                        {
                            status = SwapOfferStatus::Expired;
                        }
                        updateOffer(item.m_txId, status);
                        break;
                    }
                    case TxStatus::Canceled:
                        updateOffer(item.m_txId, SwapOfferStatus::Canceled);
                        break;
                    default:
                        // ignore
                        break;
                }
            }
        }
    }
    
    void SwapOffersBoard::updateOffer(const TxID& offerTxID, SwapOfferStatus newStatus)
    {
        if (newStatus == SwapOfferStatus::Pending) return;

        auto offerIt = m_offersCache.find(offerTxID);
        if (offerIt != m_offersCache.end())
        {
            AtomicSwapCoin coin = offerIt->second.m_coin;
            WalletID publisherId = offerIt->second.m_publisherId;
            SwapOfferStatus  currentStatus = offerIt->second.m_status;

            if (currentStatus == SwapOfferStatus::Pending)
            {
                m_offersCache[offerTxID].m_status = newStatus;
                notifySubscribers(ChangeAction::Removed, std::vector<SwapOffer>{m_offersCache[offerTxID]});
                sendUpdateToNetwork(offerTxID, publisherId, coin, newStatus);
            }
        }
        else
        {
            // Case: updateOffer() had been called before offer appeared on board.
            // Here we don't know if offer exists in bbs network at all.
            // That's why board doesn't send any update to network.
            // Instead board stores incomplete offer in cache and
            // will notify network about offer status change only
            // on receivng original 'pending-status' offer from network.
            SwapOffer incompleteOffer(offerTxID);
            incompleteOffer.m_status = newStatus;
            m_offersCache[offerTxID] = incompleteOffer;
        }
    }

    auto SwapOffersBoard::getOffersList() const -> std::vector<SwapOffer>
    {
        std::vector<SwapOffer> offers;

        for (auto offer : m_offersCache)
        {
            SwapOfferStatus status = offer.second.m_status;
            if (status == SwapOfferStatus::Pending)
            {
                offers.push_back(offer.second);
            }
        }

        return offers;
    }

    auto SwapOffersBoard::getChannel(AtomicSwapCoin coin) const -> BbsChannel
    {
        auto it = m_channelsMap.find(coin);
        assert(it != std::cend(m_channelsMap));
        return it->second;
    }

    void SwapOffersBoard::publishOffer(const SwapOffer& offer) const
    {
        auto swapCoin = offer.GetParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);
        auto isBeamSide = offer.GetParameter<bool>(TxParameterID::AtomicSwapIsBeamSide);
        auto amount = offer.GetParameter<Amount>(TxParameterID::Amount);
        auto swapAmount = offer.GetParameter<Amount>(TxParameterID::AtomicSwapAmount);
        auto responseTime = offer.GetParameter<Height>(TxParameterID::PeerResponseTime);
        auto minimalHeight = offer.GetParameter<Height>(TxParameterID::MinHeight);

        if (!swapCoin || !isBeamSide || !amount || !swapAmount || !responseTime || !minimalHeight)
        {
            LOG_WARNING() << offer.m_txId << " Can't publish invalid offer.\n\t";
            return;
        }

        LOG_INFO() << offer.m_txId << " Publish offer.\n\t"
                    << "isBeamSide: " << (*isBeamSide ? "false" : "true") << "\n\t"
                    << "swapCoin: " << std::to_string(*swapCoin) << "\n\t"
                    << "amount: " << *amount << "\n\t"
                    << "swapAmount: " << *swapAmount << "\n\t"
                    << "responseTime: " << *responseTime << "\n\t"
                    << "minimalHeight: " << *minimalHeight;
        
        auto message = m_protocolHandler.createMessage(offer, offer.m_publisherId);
        if (!message)
        {
            LOG_WARNING() << offer.m_txId << " Offer has foreign Pk and will not be published.\n\t";
            return;
        }
        WalletID dummyWId;
        dummyWId.m_Channel = getChannel(*swapCoin);
        m_messageEndpoint.SendRawMessage(dummyWId, *message);
    }

    /**
     *  Creates truncated offer w/o any unnecessary data to reduce size.
     */
    void SwapOffersBoard::sendUpdateToNetwork(const TxID& offerID, const WalletID& publisherID, AtomicSwapCoin coin, SwapOfferStatus newStatus) const
    {
        LOG_INFO() << offerID << " offer status updated to " << std::to_string(newStatus);
        auto message = m_protocolHandler.createMessage(SwapOffer(offerID, newStatus, publisherID, coin), publisherID);
        if (!message)
        {
            LOG_WARNING() << offerID << " Offer has foreign Pk and will not be updated.\n\t";
            return;
        }
        WalletID dummyWId;
        dummyWId.m_Channel = getChannel(coin);
        m_messageEndpoint.SendRawMessage(dummyWId, *message);
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

    void SwapOffersBoard::notifySubscribers(ChangeAction action, const std::vector<SwapOffer>& offers) const
    {
        for (const auto sub : m_subscribers)
        {
                sub->onSwapOffersChanged(action, std::vector<SwapOffer>{offers});
        }
    }

} // namespace beam::wallet
