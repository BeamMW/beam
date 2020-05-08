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

#include "wallet/core/common.h"
#include "wallet/transactions/swaps/common.h"

namespace beam::wallet
{
struct SwapOffer : public TxParameters
{
    SwapOffer() = default;
    SwapOffer(const boost::optional<TxID>& txID);
    SwapOffer(const TxID& txId,
              SwapOfferStatus status,
              WalletID publisherId,
              AtomicSwapCoin coin,
              bool isOwn);
    SwapOffer(const TxParameters& params);
    /**
     * Used to set m_Parameters on default constructed SwapOffer
     */
    void SetTxParameters(const PackedTxParameters&);

    bool IsValid() const;

    bool isBeamSide() const;
    Amount amountBeam() const;
    Amount amountSwapCoin() const;
    AtomicSwapCoin swapCoinType() const;
    Timestamp timeCreated() const;
    Height peerResponseHeight() const;
    Height minHeight() const;

    TxID m_txId = {};
    SwapOfferStatus m_status = SwapOfferStatus::Pending;
    WalletID m_publisherId = Zero;
    mutable AtomicSwapCoin m_coin = AtomicSwapCoin::Unknown;
    bool m_isOwn = false;
};
    
} // namespace beam::wallet
