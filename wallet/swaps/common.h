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

#include "wallet/common.h"

namespace beam::wallet
{
    constexpr uint32_t kBeamLockTimeInBlocks = 24 * 60;
    constexpr Height kBeamLockTxLifetimeMax = 12 * 60;
    constexpr Amount kMinFeeInGroth = 100;

    enum SubTxIndex : SubTxID
    {
        BEAM_LOCK_TX = 2,
        BEAM_REFUND_TX = 3,
        BEAM_REDEEM_TX = 4,
        LOCK_TX = 5,
        REFUND_TX = 6,
        REDEEM_TX = 7
    };

    enum class SwapTxState : uint8_t
    {
        Initial,
        CreatingTx,
        SigningTx,
        Constructed
    };

    uint64_t UnitsPerCoin(AtomicSwapCoin swapCoin) noexcept;
}
