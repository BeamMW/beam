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

#include "settings.h"
#include "common.h"

namespace beam::denarius
{
    using DenariusCoreSettings = bitcoin::BitcoinCoreSettings;
    using ElectrumSettings = bitcoin::ElectrumSettings;

    class Settings : public bitcoin::Settings
    {
    public:
        Settings()
            : bitcoin::Settings()
        {
            constexpr double kDBlocksPerHour = 120; //30 second blocks, 120 per hour
            constexpr uint32_t kDDefaultLockTimeInBlocks = 12 * 24;  // 12h
            constexpr Amount kDMinFeeRate = 90000;

            SetLockTimeInBlocks(kDDefaultLockTimeInBlocks);
            SetFeeRate(kDMinFeeRate);
            SetBlocksPerHour(kDBlocksPerHour);
            SetAddressVersion(getAddressVersion());
            SetGenesisBlockHashes(getGenesisBlockHashes());

            auto electrumSettings = GetElectrumConnectionOptions();

            electrumSettings.m_nodeAddresses =
            {
#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
                "electrumx1.denarius.pro:50002",
                "electrumx2.denarius.pro:50002",
                "electrumx3.denarius.pro:50002",
                "electrumx4.denarius.pro:50002"
#else // MASTERNET and TESTNET
                "electrumx1.denarius.pro:50002",
                "electrumx2.denarius.pro:50002",
                "electrumx3.denarius.pro:50002",
                "electrumx4.denarius.pro:50002"
#endif
            };

            SetElectrumConnectionOptions(electrumSettings);
        }
    };
} //namespace beam::denarius
