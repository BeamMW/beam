// Copyright 2020 The Beam Team
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
struct SwapOffer;

class SwapOfferToken
{
public:
    static bool isValid(const std::string& token);

    SwapOfferToken() = default;
    SwapOfferToken(const SwapOffer& offer);
    
    SwapOffer Unpack() const;
    SERIALIZE(m_TxID, m_status, m_publisherId, m_coin, m_Parameters);
private:
    boost::optional<TxID> m_TxID;
    boost::optional<SwapOfferStatus> m_status;
    boost::optional<WalletID> m_publisherId;
    boost::optional<AtomicSwapCoin> m_coin;
    PackedTxParameters m_Parameters;
};
}  // namespace beam::wallet
