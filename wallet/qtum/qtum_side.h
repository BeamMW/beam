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

#include "../bitcoin/bitcoin_side.h"

namespace beam::wallet
{
    class QtumSide : public BitcoinSide
    {
    public:

        QtumSide(BaseTransaction& tx, std::shared_ptr<IBitcoinBridge> bitcoinBridge, bool isBeamSide)
            : BitcoinSide(tx, bitcoinBridge, isBeamSide)
        {
        }

        uint32_t GetTxTimeInBeamBlocks() const
        {
            // it's average value
            return 30;
        }

        static bool CheckAmount(Amount amount, Amount feeRate)
        {
            constexpr uint32_t kQtumWithdrawTxAverageSize = 360;
            constexpr Amount kDustThreshold = 72800;
            Amount fee = static_cast<Amount>(std::round(double(kQtumWithdrawTxAverageSize * feeRate) / 1000));
            return amount > kDustThreshold && amount > fee;
        }
    };
}