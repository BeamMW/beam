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

namespace beam::dash
{
    using DashCoreSettings = bitcoin::BitcoinCoreSettings;
    using ElectrumSettings = bitcoin::ElectrumSettings;

    class Settings : public bitcoin::Settings
    {
    public:
        Settings()
            : bitcoin::Settings()
        {
            constexpr double kBlocksPerHour = 24;
            constexpr uint32_t kDefaultLockTimeInBlocks = 12 * 24;
            // TODO roman.strilets need investigate
            constexpr Amount kMinFeeRate = 1000;
            constexpr Amount kDefaultFeeRate = 90000;

            SetLockTimeInBlocks(kDefaultLockTimeInBlocks);
            SetFeeRate(kDefaultFeeRate);
            SetMinFeeRate(kMinFeeRate);
            SetBlocksPerHour(kBlocksPerHour);
            SetAddressVersion(getAddressVersion());
            SetGenesisBlockHashes(getGenesisBlockHashes());

            auto electrumSettings = GetElectrumConnectionOptions();

            electrumSettings.m_nodeAddresses =
            {
#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
                "drk.p2pay.com:50002",
                "electrum.dash.siampm.com:50002"
#else // MASTERNET and TESTNET
                "dword.ga:51002"
#endif
            };

            SetElectrumConnectionOptions(electrumSettings);
        }
    };
} //namespace beam::dash