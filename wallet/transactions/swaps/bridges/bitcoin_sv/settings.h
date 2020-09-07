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

namespace beam::bitcoin_sv
{
    using CoreSettings = bitcoin::BitcoinCoreSettings;
    using ElectrumSettings = bitcoin::ElectrumSettings;

    class Settings : public bitcoin::Settings
    {
    public:
        Settings()
            : bitcoin::Settings()
        {
            constexpr double kBlocksPerHour = 6;
            constexpr uint32_t kDefaultLockTimeInBlocks = 12 * 6;  // 12h
            constexpr Amount kMinFeeRate = 90000;

            SetLockTimeInBlocks(kDefaultLockTimeInBlocks);
            SetMinFeeRate(kMinFeeRate);
            SetBlocksPerHour(kBlocksPerHour);
            SetAddressVersion(getAddressVersion());
            SetGenesisBlockHashes(getGenesisBlockHashes());

            auto electrumSettings = GetElectrumConnectionOptions();

            electrumSettings.m_nodeAddresses =
            {
#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
                "sv.usebsv.com:50002",
                "electrumx.bitcoinsv.io:50002",
                "sv.satoshi.io:50002",
                "sv2.satoshi.io:50002",
                "sv.jochen-hoenicke.de:50002",
                "satoshi.vision.cash:50002",
                "electrumx-sv.1209k.com:50002"
#else // MASTERNET and TESTNET
                "tsv.usebsv.com:51002",
                "electrontest.cascharia.com:51002"
#endif
            };

            SetElectrumConnectionOptions(electrumSettings);
        }
    };
} //namespace beam::bitcoin_sv
