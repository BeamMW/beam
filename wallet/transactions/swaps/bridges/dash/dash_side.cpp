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

#include "dash_side.h"
#include "common.h"

namespace
{
    constexpr uint32_t kDashWithdrawTxAverageSize = 360; 
    constexpr uint32_t kDashLockTxEstimatedTimeInBeamBlocks = 20;   // it's average value
}

namespace beam::wallet
{
    DashSide::DashSide(BaseTransaction& tx, bitcoin::IBridge::Ptr bitcoinBridge, dash::ISettingsProvider& settingsProvider, bool isBeamSide)
        : BitcoinSide(tx, bitcoinBridge, settingsProvider, isBeamSide)
    {
    }

    bool DashSide::CheckAmount(Amount amount, Amount feeRate)
    {
        Amount fee = CalcTotalFee(feeRate);
        return amount > fee && (amount - fee) >= dash::kDustThreshold;
    }

    Amount DashSide::CalcTotalFee(Amount feeRate)
    {
        return static_cast<Amount>(std::round(double(kDashWithdrawTxAverageSize * feeRate) / 1000));
    }

    uint32_t DashSide::GetLockTxEstimatedTimeInBeamBlocks() const
    {
        return kDashLockTxEstimatedTimeInBeamBlocks;
    }

    bool DashSide::IsSegwitSupported() const
    {
        return false;
    }
}