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

#include "wallet/bitcoin/settings.h"
#include "common.h"

namespace beam::qtum
{
    using QtumCoreSettings = bitcoin::BitcoinCoreSettings;
    using ElectrumSettings = bitcoin::ElectrumSettings;

    class Settings : public bitcoin::Settings
    {
    public:
        Settings()
            : bitcoin::Settings()
        {
            constexpr uint16_t kQtumDefaultTxMinConfirmations = 10;
            constexpr double kQtumBlocksPerHour = 25;
            constexpr uint32_t kQtumDefaultLockTimeInBlocks = 12 * 25;  // 12h
            constexpr Amount kQtumMinFeeRate = 500000;

            SetTxMinConfirmations(kQtumDefaultTxMinConfirmations);
            SetLockTimeInBlocks(kQtumDefaultLockTimeInBlocks);
            SetFeeRate(kQtumMinFeeRate);
            SetMinFeeRate(kQtumMinFeeRate);
            SetBlocksPerHour(kQtumBlocksPerHour);
            SetAddressVersion(getAddressVersion());
        }
    };
} // namespace beam::qtum