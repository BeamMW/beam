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

#include "../bitcoin/settings.h"
#include "common.h"

namespace beam::dogecoin
{
    using DogecoinCoreSettings = bitcoin::BitcoinCoreSettings;
    using ElectrumSettings = bitcoin::ElectrumSettings;

    class Settings : public bitcoin::Settings
    {
    public:
        Settings()
            : bitcoin::Settings()
        {
            constexpr uint16_t kLockTxMinConfirmations = 6;
            constexpr double kDogeBlocksPerHour = 60;
            constexpr uint32_t kDogeDefaultLockTimeInBlocks = 12 * 60;
            constexpr Amount kDogeMinFeeRate = 100'000'000u;
            constexpr Amount kDogeMaxFeeRate = 100'000'000u * 25u; // 25 * COIN

            SetLockTxMinConfirmations(kLockTxMinConfirmations);
            SetLockTimeInBlocks(kDogeDefaultLockTimeInBlocks);
            SetMinFeeRate(kDogeMinFeeRate);
            SetMaxFeeRate(kDogeMaxFeeRate);
            SetBlocksPerHour(kDogeBlocksPerHour);
            SetAddressVersion(getAddressVersion());
            SetGenesisBlockHashes(getGenesisBlockHashes());
            DisableElectrum();
        }
    };
} //namespace beam::dogecoin