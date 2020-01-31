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

#include "wallet/transactions/swaps/swap_offer_token.h"
#include "wallet/transactions/swaps/swap_offer.h"

namespace beam::wallet
{
// static 
bool SwapOfferToken::isValid(const std::string& token)
{
    if (token.empty()) return false;
    
    auto params = ParseParameters(token);
    if (!params)
    {
        return false;
    }
    auto type = params->GetParameter<TxType>(TxParameterID::TransactionType);
    return type && *type == TxType::AtomicSwap;
}

SwapOfferToken::SwapOfferToken(const SwapOffer& offer)
    : m_TxID(offer.m_txId),
      m_status(offer.m_status),
      m_publisherId(offer.m_publisherId),
      m_coin(offer.m_coin),
      m_Parameters(offer.Pack()) {}

SwapOffer SwapOfferToken::Unpack() const
{
    SwapOffer result(m_TxID);
    result.SetTxParameters(m_Parameters);

    if (m_TxID) result.m_txId = *m_TxID;
    if (m_status) result.m_status = *m_status;
    if (m_publisherId) result.m_publisherId = *m_publisherId;
    if (m_coin) result.m_coin = *m_coin;
    return result;
}
}  // namespace beam::wallet
