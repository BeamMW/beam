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
class SwapOffersBoard;
class IAtomicSwapProvider
{
public:
    virtual Amount getCoinAvailable(AtomicSwapCoin swapCoin) const = 0;
    virtual Amount getRecommendedFeeRate(AtomicSwapCoin swapCoin) const = 0;
    virtual Amount getMinFeeRate(AtomicSwapCoin swapCoin) const = 0;
    virtual Amount getMaxFeeRate(AtomicSwapCoin swapCoin) const = 0;
    virtual const SwapOffersBoard& getSwapOffersBoard() const = 0;
    virtual bool isCoinClientConnected(AtomicSwapCoin swapCoin) const = 0;
};
}  // namespace beam::wallet
