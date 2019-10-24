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
        for (auto channel : m_channelsMap)
        {
            m_network.BbsSubscribe(channel.second, m_lastTimestamp, this);
        }
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
        
        auto newOffer = token.Unpack();

        confirmation.m_offerData = toByteBuffer(token);
                    
        if (!confirmation.IsValid(newOffer.m_publisherId.m_Pk))
        {
            LOG_WARNING() << "offer board message signature is invalid";
            return;
        }

        if (newOffer.m_coin >= AtomicSwapCoin::Unknown || newOffer.m_status > SwapOfferStatus::Failed)
        {
            LOG_WARNING() << "offer board message is invalid";
            return;
        }

        auto it = m_offersCache.find(newOffer.m_txId);
        // New offer
        if (it == m_offersCache.end())
        {
            m_offersCache[newOffer.m_txId] = newOffer;

            if (newOffer.m_status == SwapOfferStatus::Pending)
            {
                notifySubscribers(ChangeAction::Added, std::vector<SwapOffer>{newOffer});
            }
            else
            {
                // Don't push irrelevant offers to subscribers
            }
        }
        // Existing offer update
        else    
        {
            SwapOfferStatus existingStatus = m_offersCache[newOffer.m_txId].m_status;

            // Normal case
            if (existingStatus == SwapOfferStatus::Pending)
            {
                if (newOffer.m_status != SwapOfferStatus::Pending)
                {
                    m_offersCache[newOffer.m_txId].m_status = newOffer.m_status;
                    notifySubscribers(ChangeAction::Removed, std::vector<SwapOffer>{newOffer});
                }
            }
            // Transaction state has changed asynchronously while board was offline.
            // Incomplete offer with SwapOfferStatus!=Pending was created.
            // If offer with SwapOfferStatus::Pending is still exist in network,
            // it need to be updated to latest status.
            else
            {
                if (newOffer.m_status == SwapOfferStatus::Pending)
                {
                    sendUpdateToNetwork(newOffer.m_txId, newOffer.m_publisherId, newOffer.m_coin, existingStatus);
                }
            }
        }
    }

    /**
     *  Watches for system state to remove stuck expired offers from board.
     *  Notify only subscribers. Doesn't push any updates to network.
     */
    void SwapOffersBoard::onSystemStateChanged(const Block::SystemState::ID& stateID)
    {
        Height currentHeight = stateID.m_Height;

        for (auto& pair : m_offersCache)
        {
            if (pair.second.m_status != SwapOfferStatus::Pending) continue;    // have to be already removed from board

            auto peerResponseTime = pair.second.GetParameter<Height>(TxParameterID::PeerResponseTime);
            auto minHeight = pair.second.GetParameter<Height>(TxParameterID::MinHeight);
            if (peerResponseTime && minHeight)
            {
                auto expiresHeight = *minHeight + *peerResponseTime;

                if (expiresHeight <= currentHeight)
                {
                    pair.second.m_status = SwapOfferStatus::Expired;
                    notifySubscribers(ChangeAction::Removed, std::vector<SwapOffer>{pair.second});
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
            // Here we don't know if offer exists in network at all. So board doesn't send any update to network.
            // Board stores incomplete offer to notify network when original Pending offer will be received from network.
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
        
        beam::wallet::SwapOfferToken token(offer);
        m_messageEndpoint.SendAndSign(toByteBuffer(token), getChannel(*swapCoin), offer.m_publisherId, m_protocolVersion);
    }

    void SwapOffersBoard::sendUpdateToNetwork(const TxID& offerID, const WalletID& publisherID, AtomicSwapCoin coin, SwapOfferStatus newStatus) const
    {
        LOG_INFO() << offerID << " Update offer status to " << std::to_string(newStatus);

        beam::wallet::SwapOfferToken token(SwapOffer(offerID, newStatus, publisherID, coin));
        m_messageEndpoint.SendAndSign(toByteBuffer(token), getChannel(coin), publisherID, m_protocolVersion);
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
        for (auto sub : m_subscribers)
        {
            sub->onSwapOffersChanged(action, std::vector<SwapOffer>{offers});
        }
    }

} // namespace beam::wallet
