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

#include "denarius_side.h"
#include "common.h"

namespace
{
    constexpr uint32_t kDenariusWithdrawTxAverageSize = 360; 
    constexpr uint32_t kDenariusLockTxEstimatedTimeInBeamBlocks = 20;   // it's average value
}

namespace beam::wallet
{
    DenariusSide::DenariusSide(BaseTransaction& tx, bitcoin::IBridge::Ptr bitcoinBridge, denarius::ISettingsProvider& settingsProvider, bool isBeamSide)
        : BitcoinSide(tx, bitcoinBridge, settingsProvider, isBeamSide)
    {
    }

    bool DenariusSide::CheckAmount(Amount amount, Amount feeRate)
    {
        Amount fee = static_cast<Amount>(std::round(double(kDenariusWithdrawTxAverageSize * feeRate) / 1000));
        return amount > denarius::kDustThreshold && amount > fee;
    }

    Amount DenariusSide::CalcTotalFee(Amount feeRate)
    {
        return static_cast<Amount>(std::round(double(kDenariusWithdrawTxAverageSize * feeRate) / 1000));
    }

    uint32_t DenariusSide::GetLockTxEstimatedTimeInBeamBlocks() const
    {
        return kDenariusLockTxEstimatedTimeInBeamBlocks;
    }
}