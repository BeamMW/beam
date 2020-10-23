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

#include "dogecoin_side.h"
#include "common.h"

namespace
{
    // TODO check these parameters
    constexpr uint32_t kDogecoinWithdrawTxAverageSize = 360; 
    constexpr uint32_t kDogecoinLockTxEstimatedTimeInBeamBlocks = 20;   // it's average value
}

namespace beam::wallet
{
    DogecoinSide::DogecoinSide(BaseTransaction& tx, bitcoin::IBridge::Ptr bitcoinBridge, dogecoin::ISettingsProvider& settingsProvider, bool isBeamSide)
        : BitcoinSide(tx, bitcoinBridge, settingsProvider, isBeamSide)
    {
    }

    bool DogecoinSide::CheckAmount(Amount amount, Amount feeRate)
    {
        Amount fee = CalcTotalFee(feeRate);
        return amount > fee && (amount - fee) >= dogecoin::kDustThreshold;
    }

    Amount DogecoinSide::CalcTotalFee(Amount feeRate)
    {
        Amount factor = kDogecoinWithdrawTxAverageSize / 1000 + (kDogecoinWithdrawTxAverageSize % 1000 ? 1 : 0);
        return factor * feeRate;
    }

    uint32_t DogecoinSide::GetLockTxEstimatedTimeInBeamBlocks() const
    {
        return kDogecoinLockTxEstimatedTimeInBeamBlocks;
    }

    bool DogecoinSide::IsSegwitSupported() const
    {
        return false;
    }

    uint32_t DogecoinSide::GetWithdrawTxAverageSize() const
    {
        return kDogecoinWithdrawTxAverageSize;
    }

    Amount DogecoinSide::GetFee(Amount feeRate) const
    {
        return CalcTotalFee(feeRate);
    }
}