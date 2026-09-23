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

#include <algorithm>
#include <cctype>

namespace beam::wallet
{
namespace
{
    // Token symbol as reported by a peer's ERC-20 contract: bounded length,
    // printable ASCII only (0x20-0x7E), never empty.
    bool isValidTokenSymbol(const std::string& symbol)
    {
        if (symbol.empty() || symbol.size() > 32)
        {
            return false;
        }
        return std::all_of(symbol.begin(), symbol.end(), [](unsigned char c)
        {
            return c >= 0x20 && c <= 0x7E;
        });
    }
}  // namespace
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

bool SwapOffersBoard::onMessage(BroadcastMsg&& msg)
{
    auto newOffer = m_protocolHandler.parseMessage(msg);
    if (!newOffer) return false;

    return onOfferFromNetwork(*newOffer);
}

/**
 *  Watches for system state to remove stuck expired offers from board.
 *  Doesn't push any updates to network, just notify subscribers.
 */
void SwapOffersBoard::onSystemStateChanged(const HeightHash& stateID)
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
            m_ownAddresses.emplace(address.m_BbsAddr, address.m_OwnID);
            break;
        case ChangeAction::Removed:
            m_ownAddresses.erase(address.m_BbsAddr);
            break;
        case ChangeAction::Updated:
            // m_BbsAddr or m_OwnID shouldn't change
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

        BEAM_LOG_INFO() << offer.m_txId << " Publish offer.\n\t"
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
        m_ownAddresses.emplace(address.m_BbsAddr, address.m_OwnID);
    }
}

bool SwapOffersBoard::onOfferFromNetwork(SwapOffer& newOffer)
{
    // ExtendedOffer (the wire placeholder for asset/ERC-20 offers) is
    // accepted and validated below; only true unknowns are rejected outright.
    // Erc20Token must never appear as the top-level wire coin: a legitimate
    // publisher always substitutes ExtendedOffer on the wire and carries the
    // real Erc20Token coin inside the AtomicSwapCoin tx param (see
    // SwapOffersBoard::broadcastOffer / SwapOffer::IsExtended). A raw wire
    // Erc20Token is therefore malformed by definition and rejected outright.
    if (newOffer.m_coin > AtomicSwapCoin::ExtendedOffer ||
        newOffer.m_coin == AtomicSwapCoin::Erc20Token ||
        newOffer.m_status > SwapOfferStatus::Failed)
    {
        BEAM_LOG_WARNING() << "offer board message is invalid";
        return false;
    }

    if (newOffer.m_coin == AtomicSwapCoin::ExtendedOffer)
    {
        if (!isExtendedOfferDataValid(newOffer))
        {
            BEAM_LOG_WARNING() << "offer board message is invalid";
            return false;
        }
        // ExtendedOffer is only a wire placeholder (kept old wallets from
        // mis-parsing the coin); internally we always work with the real coin.
        newOffer.m_coin = newOffer.ResolveCoin();
    }

    auto it = m_offersCache.find(newOffer.m_txId);

    if (it == m_offersCache.end()) // New offer
    {
        newOffer.m_isOwn = isOwnOffer(newOffer);
        if (newOffer.m_status == SwapOfferStatus::Pending)
        {
            if (!newOffer.IsValid())
            {
                BEAM_LOG_WARNING() << "incoming offer is invalid";
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
                // the incomplete cache entry carries only txId + status; adopt
                // the full parameter set from the network copy so the update
                // broadcast is valid (extended offers need the packed
                // coin/token params on the wire), then send the stored status
                newOffer.m_status = existingOffer.m_status;
                newOffer.m_isOwn = true;
                existingOffer = newOffer;
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

/**
 *  Validates the payload of an incoming offer wire-tagged as ExtendedOffer:
 *  the real foreign coin must resolve and not be Unknown, and the params
 *  required to interpret it must be present.
 */
bool SwapOffersBoard::isExtendedOfferDataValid(const SwapOffer& offer) const
{
    auto resolvedCoin = offer.ResolveCoin();
    if (resolvedCoin == AtomicSwapCoin::Unknown || resolvedCoin == AtomicSwapCoin::ExtendedOffer)
    {
        return false;
    }

    if (resolvedCoin == AtomicSwapCoin::Erc20Token)
    {
        auto decimals = offer.GetParameter<uint8_t>(TxParameterID::AtomicSwapTokenDecimals);
        auto contract = offer.GetParameter<std::string>(TxParameterID::AtomicSwapTokenContract);
        auto symbol = offer.GetParameter<std::string>(TxParameterID::AtomicSwapTokenSymbol);
        if (!contract || !symbol ||
            !decimals || *decimals > kMaxTokenDecimals ||
            !IsValidEthContractAddress(*contract) ||
            !isValidTokenSymbol(*symbol))
        {
            return false;
        }
    }

    auto beamAssetId = offer.GetParameter<Asset::ID>(TxParameterID::AtomicSwapBeamAssetID);
    if (beamAssetId.value_or(0) != 0)
    {
        if (!offer.GetParameter<std::string>(TxParameterID::AtomicSwapBeamAssetName))
        {
            return false;
        }
    }

    return true;
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
    BEAM_LOG_INFO() << offer.m_txId << " offer status updated to " << std::to_string(offer.m_status);

    auto it = m_ownAddresses.find(offer.m_publisherId);
    
    if (it != std::cend(m_ownAddresses))
    {
        broadcastOffer(offer, it->second/*m_OwnID*/);
    }
}

void SwapOffersBoard::broadcastOffer(const SwapOffer& offer, uint64_t keyOwnID) const
{
    // Extended offers (foreign-asset-on-Beam-side or ERC-20 foreign coin) are wire-
    // tagged with the ExtendedOffer placeholder so old wallets - which reject
    // m_coin >= (old) Unknown - drop them instead of misinterpreting the coin.
    // The real coin stays available to new wallets via the AtomicSwapCoin tx param.
    static_assert(static_cast<int32_t>(AtomicSwapCoin::ExtendedOffer) >= 10,
        "ExtendedOffer must be >= the pre-extension AtomicSwapCoin::Unknown ordinal (10) "
        "so old wallets' 'm_coin >= Unknown' guard drops extended offers");

    if (offer.IsExtended())
    {
        SwapOffer wireOffer = offer;
        wireOffer.m_coin = AtomicSwapCoin::ExtendedOffer;
        auto message = m_protocolHandler.createBroadcastMessage(wireOffer, keyOwnID);
        m_broadcastGateway.sendMessage(BroadcastContentType::SwapOffers, message);
        return;
    }

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
