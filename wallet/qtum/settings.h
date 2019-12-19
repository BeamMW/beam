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
            constexpr double kQtumBlocksPerHour = 28.125;
            constexpr uint32_t kQtumDefaultLockTimeInBlocks = static_cast<uint32_t>(12 * kQtumBlocksPerHour);  // 12h
            constexpr Amount kQtumMinFeeRate = 500000;

            SetTxMinConfirmations(kQtumDefaultTxMinConfirmations);
            SetLockTimeInBlocks(kQtumDefaultLockTimeInBlocks);
            SetFeeRate(kQtumMinFeeRate);
            SetBlocksPerHour(kQtumBlocksPerHour);
            SetAddressVersion(getAddressVersion());
            SetGenesisBlockHashes(getGenesisBlockHashes());

            auto electrumSettings = GetElectrumConnectionOptions();

            electrumSettings.m_nodeAddresses =
            {
#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
                "s1.qtum.info:50002",
                "s2.qtum.info:50002",
                "s3.qtum.info:50002",
                "s4.qtum.info:50002",
                "s5.qtum.info:50002",
                "s7.qtum.info:50002",
                "s8.qtum.info:50002",
                "s9.qtum.info:50002"
#else // MASTERNET and TESTNET
                "s1.qtum.info:51002",
                "s2.qtum.info:51002",
                "s3.qtum.info:51002"
#endif
            };

            SetElectrumConnectionOptions(electrumSettings);
        }
    };
} // namespace beam::qtum