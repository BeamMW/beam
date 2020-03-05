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

#include "swap_offer_token.h"

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
    SwapOffer offer(*params);
    return offer.IsValid();
}

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

boost::optional<WalletID> SwapOfferToken::getPublicKey() const
{
    return m_publisherId;
}

} // namespace beam::wallet
