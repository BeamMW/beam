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

#include "wallet/client/extensions/offers_board/swap_offers_board.h"

#include "utility/logger.h"

namespace beam::wallet
{
/**
 *  @broadcastRouter    incoming messages source
 *  @messageEndpoint    outgoing messages destination
 *  @protocolHandler    offer board protocol handler
 */
SwapOffersBoard::SwapOffersBoard(IBroadcastMsgGateway& broadcastGateway,
                                 OfferBoardProtocolHandler& protocolHandler,
                                 IWalletDB::Ptr walletDB)
    : m_broadcastGateway(broadcastGateway),
      m_protocolHandler(protocolHandler),
      m_walletDB(walletDB)
{
    broadcastGateway.registerListener(BroadcastContentType::SwapOffers, this);
    fillOwnAdresses();
}

bool SwapOffersBoard::onMessage(uint64_t unused, BroadcastMsg&& msg)
{
    auto newOffer = m_protocolHandler.parseMessage(msg);
    if (!newOffer) return false;

    return onOfferFromNetwork(*newOffer);
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

void SwapOffersBoard::onAddressChanged(ChangeAction action, const std::vector<WalletAddress>& items)
{
    for (const auto& address : items)
    {
        if (!address.isOwn()) continue;

        switch (action)
        {
        case ChangeAction::Reset:
        case ChangeAction::Added:
            m_ownAddresses.emplace(address.m_walletID, address.m_OwnID);
            break;
        case ChangeAction::Removed:
            m_ownAddresses.erase(address.m_walletID);
            break;
        case ChangeAction::Updated:
            // m_walletID or m_OwnID shouldn't change
        default:
            break;
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
                    updateOfferStatus(item.m_txId, SwapOfferStatus::InProgress);
                    break;
                case TxStatus::Failed:
                {
                    auto reason = item.GetParameter<TxFailureReason>(TxParameterID::InternalFailureReason);
                    SwapOfferStatus status = SwapOfferStatus::Failed;

                    if (reason && *reason == TxFailureReason::TransactionExpired)
                    {
                        status = SwapOfferStatus::Expired;
                    }
                    updateOfferStatus(item.m_txId, status);
                    break;
                }
                case TxStatus::Canceled:
                    updateOfferStatus(item.m_txId, SwapOfferStatus::Canceled);
                    break;
                default:
                    // ignore
                    break;
            }
        }
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

void SwapOffersBoard::publishOffer(const SwapOffer& offer) const
{
    if (!offer.IsValid())
    {
        throw InvalidOfferException();
    }

    if (isOfferExpired(offer))
    {
        throw ExpiredOfferException();
    }

    if (isOfferLifetimeTooLong(offer))
    {
        throw OfferLifetimeExceeded();
    }

    if (auto offerIt = m_offersCache.find(offer.m_txId); offerIt != m_offersCache.end())
    {
        throw OfferAlreadyPublishedException();
    }
    
    auto it = m_ownAddresses.find(offer.m_publisherId);
    
    if (it == std::cend(m_ownAddresses))
    {
        throw ForeignOfferException();
    }

    {
        auto swapCoin = offer.GetParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);
        auto isBeamSide = offer.GetParameter<bool>(TxParameterID::AtomicSwapIsBeamSide);
        auto amount = offer.GetParameter<Amount>(TxParameterID::Amount);
        auto swapAmount = offer.GetParameter<Amount>(TxParameterID::AtomicSwapAmount);
        auto responseTime = offer.GetParameter<Height>(TxParameterID::PeerResponseTime);
        auto minimalHeight = offer.GetParameter<Height>(TxParameterID::MinHeight);

        LOG_INFO() << offer.m_txId << " Publish offer.\n\t"
            << "isBeamSide: " << (*isBeamSide ? "false" : "true") << "\n\t"
            << "swapCoin: " << std::to_string(*swapCoin) << "\n\t"
            << "amount: " << *amount << "\n\t"
            << "swapAmount: " << *swapAmount << "\n\t"
            << "responseTime: " << *responseTime << "\n\t"
            << "minimalHeight: " << *minimalHeight;
    }

    broadcastOffer(offer, it->second/*m_OwnID*/);
}

void SwapOffersBoard::fillOwnAdresses()
{
    const auto addresses = m_walletDB->getAddresses(true);

    for (const auto& address : addresses)
    {
        m_ownAddresses.emplace(address.m_walletID, address.m_OwnID);
    }
}

bool SwapOffersBoard::onOfferFromNetwork(SwapOffer& newOffer)
{
    if (newOffer.m_coin >= AtomicSwapCoin::Unknown || newOffer.m_status > SwapOfferStatus::Failed)
    {
        LOG_WARNING() << "offer board message is invalid";
        return false;
    }

    auto it = m_offersCache.find(newOffer.m_txId);

    if (it == m_offersCache.end()) // New offer
    {
        newOffer.m_isOwn = isOwnOffer(newOffer);
        if (newOffer.m_status == SwapOfferStatus::Pending)
        {
            if (!newOffer.IsValid())
            {
                LOG_WARNING() << "incoming offer is invalid";
                return false;
            }
            if (isOfferExpired(newOffer))
            {
                newOffer.m_status = SwapOfferStatus::Expired;
            }
            notifySubscribers(ChangeAction::Added, std::vector<SwapOffer>{ newOffer });
        }
        m_offersCache.emplace(newOffer.m_txId, newOffer);
    }
    else // Offer already exist
    {
        SwapOffer& existingOffer = it->second;

        // Normal case
        if (existingOffer.m_status == SwapOfferStatus::Pending)
        {
            if (newOffer.m_status != SwapOfferStatus::Pending)
            {
                existingOffer.m_status = newOffer.m_status;
                notifySubscribers(ChangeAction::Removed, std::vector<SwapOffer>{ newOffer });
            }
        }
        // Transaction state has changed asynchronously while board was offline.
        // Incomplete offer with SwapOfferStatus!=Pending was created.
        // If offer with SwapOfferStatus::Pending is still exist in network,
        // it need to be updated to latest status.
        else
        {
            if (newOffer.m_status == SwapOfferStatus::Pending && isOwnOffer(newOffer))
            {
                // fill missing parameters and send stored status to network
                existingOffer.m_coin = newOffer.m_coin;
                existingOffer.m_publisherId = newOffer.m_publisherId;
                existingOffer.m_isOwn = true;
                sendUpdateToNetwork(existingOffer);
            }
        }
    }
    return true;
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

/**
 *  Offers should not have lifetime longer than
 *  underlaying BBS transport message lifetime.
 *  Otherwise they will not exist in network for all lifetime.
 */ 
bool SwapOffersBoard::isOfferLifetimeTooLong(const SwapOffer& offer) const
{
    auto peerResponseTime = offer.GetParameter<Height>(TxParameterID::PeerResponseTime);
    auto minHeight = offer.GetParameter<Height>(TxParameterID::MinHeight);
    if (peerResponseTime && minHeight)
    {
        auto expiresHeight = *minHeight + *peerResponseTime;
        Height messageLifetime = m_broadcastGateway.m_bbsTimeWindow / 60; // minutes ~ blocks
        return m_currentHeight + messageLifetime < expiresHeight;
    }
    else return true;
}

bool SwapOffersBoard::isOwnOffer(const SwapOffer& offer) const
{
    return m_ownAddresses.find(offer.m_publisherId) != std::cend(m_ownAddresses);
}

void SwapOffersBoard::updateOfferStatus(const TxID& offerTxID, SwapOfferStatus newStatus)
{
    if (newStatus == SwapOfferStatus::Pending) return;

    auto offerIt = m_offersCache.find(offerTxID);
    if (offerIt != m_offersCache.end())
    {
        SwapOffer& existingOffer = offerIt->second;

        if (existingOffer.m_status == SwapOfferStatus::Pending)
        {
            existingOffer.m_status = newStatus;

            notifySubscribers(ChangeAction::Removed, std::vector<SwapOffer>{ existingOffer });
            
            if (isOwnOffer(existingOffer))
            {
                sendUpdateToNetwork(existingOffer);
            }
        }
    }
    else
    {
        // Case: Function had been called before offer appeared on board.
        // Here we don't know if offer exists in bbs network at all.
        // That's why board doesn't send any update to network.
        // Instead board stores incomplete offer in cache and
        // will notify network about offer status change only
        // on receivng original 'pending-status' offer from network.
        SwapOffer incompleteOffer(offerTxID);
        incompleteOffer.m_status = newStatus;
        m_offersCache.emplace(offerTxID, incompleteOffer);
    }
}


/**
 *  Creates truncated offer w/o any unnecessary data to reduce size.
 */
void SwapOffersBoard::sendUpdateToNetwork(const SwapOffer& offer) const
{
    LOG_INFO() << offer.m_txId << " offer status updated to " << std::to_string(offer.m_status);

    auto it = m_ownAddresses.find(offer.m_publisherId);
    
    if (it != std::cend(m_ownAddresses))
    {
        broadcastOffer(offer, it->second/*m_OwnID*/);
    }
}

void SwapOffersBoard::broadcastOffer(const SwapOffer& offer, uint64_t keyOwnID) const
{
    auto message = m_protocolHandler.createBroadcastMessage(offer, keyOwnID);
    m_broadcastGateway.sendMessage(BroadcastContentType::SwapOffers, message);
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
