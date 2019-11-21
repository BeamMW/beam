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

#include "wallet/qtum/qtum_side.h"
#include "common.h"

namespace
{
    constexpr uint32_t kQtumWithdrawTxAverageSize = 360;
    constexpr beam::Amount kQtumDustThreshold = 72800;
    constexpr uint32_t kQtumLockTxEstimatedTimeInBeamBlocks = 30;   // it's average value
}

namespace beam::wallet
{
    QtumSide::QtumSide(BaseTransaction& tx, bitcoin::IBridge::Ptr bitcoinBridge, qtum::ISettingsProvider& settingsProvider, bool isBeamSide)
        : BitcoinSide(tx, bitcoinBridge, settingsProvider, isBeamSide)
    {
    }

    bool QtumSide::CheckAmount(Amount amount, Amount feeRate)
    {
        Amount fee = static_cast<Amount>(std::round(double(kQtumWithdrawTxAverageSize * feeRate) / 1000));
        return amount > kQtumDustThreshold && amount > fee;
    }

    uint32_t QtumSide::GetLockTxEstimatedTimeInBeamBlocks() const
    {
        return kQtumLockTxEstimatedTimeInBeamBlocks;
    }
}