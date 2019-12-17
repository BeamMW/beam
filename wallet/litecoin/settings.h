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

namespace beam::litecoin
{
    using LitecoinCoreSettings = bitcoin::BitcoinCoreSettings;
    using ElectrumSettings = bitcoin::ElectrumSettings;

    class Settings : public bitcoin::Settings
    {
    public:
        Settings()
            : bitcoin::Settings()
        {
            constexpr double kLTCBlocksPerHour = 24;
            constexpr uint32_t kLTCDefaultLockTimeInBlocks = 12 * 24;  // 12h
            constexpr Amount kLtcMinFeeRate = 90000;

            SetLockTimeInBlocks(kLTCDefaultLockTimeInBlocks);
            SetFeeRate(kLtcMinFeeRate);
            SetBlocksPerHour(kLTCBlocksPerHour);
            SetAddressVersion(getAddressVersion());
            SetGenesisBlockHashes(getGenesisBlockHashes());

            auto electrumSettings = GetElectrumConnectionOptions();

            electrumSettings.m_nodeAddresses =
            {
#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
                "backup.electrum-ltc.org:443",
                "btc.cihar.com:60002",
                "electrum-ltc.bysh.me:50002",
                "electrum.ltc.xurious.com:50002",
                "ltc.litepay.ch.org:50022"
#else // MASTERNET and TESTNET
                "electrum.ltc.xurious.com:51002",
                "electrum-ltc.bysh.me:51002"
#endif
            };

            SetElectrumConnectionOptions(electrumSettings);
        }
    };
} //namespace beam::litecoin