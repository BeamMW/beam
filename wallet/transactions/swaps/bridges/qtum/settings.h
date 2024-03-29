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
#include "../../common.h"

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
            constexpr uint16_t kLockTxMinConfirmations = 10;
            constexpr double kBlocksPerHour = 28.125;
            constexpr uint32_t kDefaultLockTimeInBlocks = static_cast<uint32_t>(12 * kBlocksPerHour);  // 12h
            constexpr Amount kMinFeeRate = 400'000u;
            constexpr Amount kMaxFeeRate = 100'000'000u; // 1 * COIN

            SetLockTxMinConfirmations(kLockTxMinConfirmations);
            SetLockTimeInBlocks(kDefaultLockTimeInBlocks);
            SetMinFeeRate(kMinFeeRate);
            SetMaxFeeRate(kMaxFeeRate);
            SetBlocksPerHour(kBlocksPerHour);
            SetAddressVersion(getAddressVersion());
            SetGenesisBlockHashes(getGenesisBlockHashes());

            auto electrumSettings = GetElectrumConnectionOptions();

            if (wallet::UseMainnetSwap())
            {
                electrumSettings.m_nodeAddresses =
                {
                    "s1.qtum.info:50002",
                    "s3.qtum.info:50002",
                    "s4.qtum.info:50002",
                    "s5.qtum.info:50002",
                    "s7.qtum.info:50002",
                    "s8.qtum.info:50002",
                    "s9.qtum.info:50002",
                    "s10.qtum.info:50002"
                };
            }
            else
            {
                electrumSettings.m_nodeAddresses =
                {
                    "s2.qtum.info:51002"
                };
            }

            SetElectrumConnectionOptions(electrumSettings);
        }
    };
} // namespace beam::qtum
