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

#pragma once

#include "wallet/client/extensions/offers_board/swap_offer.h"
#include "wallet/client/extensions/offers_board/swap_offers_observer.h"
#include "wallet/client/extensions/offers_board/offers_protocol_handler.h"
#include "wallet/client/extensions/broadcast_gateway/interface.h"

#include "wallet/core/wallet.h"
#include "utility/std_extension.h"

#include <unordered_map>

namespace beam::wallet
{
using namespace beam::proto;

/**
 *  Implementation of public swap offers bulletin board using not crypted BBS broadcasting.
 */
class SwapOffersBoard
    : public IBroadcastListener,
      public IWalletDbObserver
{
public:
    using Ptr = std::shared_ptr<SwapOffersBoard>;

    class InvalidOfferException : public std::runtime_error
    {
    public:
        InvalidOfferException() : std::runtime_error(" Can't publish invalid offer.") {}
    };

    class OfferAlreadyPublishedException : public std::runtime_error
    {
    public:
        OfferAlreadyPublishedException() : std::runtime_error(" Offer has already been published.") {}
    };

    class ForeignOfferException : public std::runtime_error
    {
    public:
        ForeignOfferException() : std::runtime_error(" Offer has foreign Pk and will not be published.") {}
    };

    class ExpiredOfferException : public std::runtime_error
    {
    public:
        ExpiredOfferException() : std::runtime_error(" Can't publish expired offer.") {}
    };

    class OfferLifetimeExceeded : public std::runtime_error
    {
    public:
        OfferLifetimeExceeded() : std::runtime_error(" Offer lifetime exceeded.") {}
    };

    SwapOffersBoard(IBroadcastMsgGateway&, OfferBoardProtocolHandler&, IWalletDB::Ptr);
    virtual ~SwapOffersBoard() {};

    /**
     *  IBroadcastListener implementation
     *  Processes broadcast messages
     */
    bool onMessage(BroadcastMsg&&) override;
    
    /**
     *  IWalletDbObserver implementation
     *  Watches for swap transaction status changes to update linked offers on board
     */
    void onTransactionChanged(ChangeAction action, const std::vector<TxDescription>& items) override;
    /**
     *  Watches for system state to remove stuck expired offers from board
     */
    void onSystemStateChanged(const Block::SystemState::ID& stateID) override;
    /**
     *  Addresses used to check whether offer own or foreign
     */
    void onAddressChanged(ChangeAction action, const std::vector<WalletAddress>& items) override;

    auto getOffersList() const -> std::vector<SwapOffer>;
    void publishOffer(const SwapOffer& offer) const;

    void Subscribe(ISwapOffersObserver* observer);
    void Unsubscribe(ISwapOffersObserver* observer);

private:
    IBroadcastMsgGateway& m_broadcastGateway;
    OfferBoardProtocolHandler& m_protocolHandler;       /// handles message creating and parsing
    const IWalletDB::Ptr m_walletDB;

    std::unordered_map<TxID, SwapOffer> m_offersCache;
    std::vector<ISwapOffersObserver*> m_subscribers;    /// used to notify subscribers about offers changes
    Height m_currentHeight = 0;
    std::unordered_map<WalletID, uint64_t> m_ownAddresses;  /// Own WalletID's with BBS KDF OwnID's

    bool isOfferExpired(const SwapOffer&) const;
    bool isOfferLifetimeTooLong(const SwapOffer&) const;
    bool onOfferFromNetwork(SwapOffer& newOffer);
    void broadcastOffer(const SwapOffer& content, uint64_t keyOwnID) const;
    void sendUpdateToNetwork(const SwapOffer&) const;
    void updateOfferStatus(const TxID& offerTxID, SwapOfferStatus newStatus);
    void notifySubscribers(ChangeAction action, const std::vector<SwapOffer>& offers) const;
    void fillOwnAdresses();
    bool isOwnOffer(const SwapOffer&) const;
};

} // namespace beam::wallet
