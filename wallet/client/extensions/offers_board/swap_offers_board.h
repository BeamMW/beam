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

    SwapOffersBoard(IBroadcastMsgGateway&, OfferBoardProtocolHandler&);

    /**
     *  IBroadcastListener implementation
     *  Processes broadcast messages
     */
    virtual bool onMessage(uint64_t unused, ByteBuffer&&) override;
    
    /**
     *  IWalletDbObserver implementation
     *  Watches for swap transaction status changes to update linked offers on board
     */
    virtual void onTransactionChanged(ChangeAction action, const std::vector<TxDescription>& items) override;
    /**
     *  Watches for system state to remove stuck expired offers from board
     */
    virtual void onSystemStateChanged(const Block::SystemState::ID& stateID) override;

    auto getOffersList() const -> std::vector<SwapOffer>;
    void publishOffer(const SwapOffer& offer) const;

    void Subscribe(ISwapOffersObserver* observer);
    void Unsubscribe(ISwapOffersObserver* observer);

private:
    IBroadcastMsgGateway& m_broadcastGateway;
    OfferBoardProtocolHandler& m_protocolHandler;       /// handles message creating and parsing

    Height m_currentHeight = 0;
    std::unordered_map<TxID, SwapOffer> m_offersCache;
    std::vector<ISwapOffersObserver*> m_subscribers;    /// used to notify subscribers about offers changes

    bool isOfferExpired(const SwapOffer& offer) const;
    void sendUpdateToNetwork(const TxID&, const WalletID&, AtomicSwapCoin, SwapOfferStatus) const;
    void updateOffer(const TxID& offerTxID, SwapOfferStatus newStatus);
    void notifySubscribers(ChangeAction action, const std::vector<SwapOffer>& offers) const;

};

} // namespace beam::wallet
