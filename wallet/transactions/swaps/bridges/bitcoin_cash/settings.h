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

namespace beam::bitcoin_cash
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
            constexpr Amount kMinFeeRate = 1000;

            SetLockTimeInBlocks(kDefaultLockTimeInBlocks);
            SetMinFeeRate(kMinFeeRate);
            SetBlocksPerHour(kBlocksPerHour);
            SetAddressVersion(getAddressVersion());
            SetGenesisBlockHashes(getGenesisBlockHashes());

            auto electrumSettings = GetElectrumConnectionOptions();

            electrumSettings.m_nodeAddresses =
            {
#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
                "bch.crypto.mldlabs.com:50002",
                "bch.cyberbits.eu:50002",
                "bch.disdev.org:50002",
                "bch.imaginary.cash:50002",
                "bch.loping.net:50002",
                "bch.soul-dev.com:50002",
                "bch0.kister.net:50002",
                "bch2.electroncash.dk:50002",
                "bitcoincash.network:50002",
                "bitcoincash.quangld.com:50002",
                "blackie.c3-soft.com:50002",
                "ec-bcn.criptolayer.net:50212",
                "electron.jochen-hoenicke.de:51002",
                "electroncash.de:50002",
                "electroncash.dk:50002",
                "electrs.bitcoinunlimited.info:50002",
                "electrum.imaginary.cash:50002",
                "electrumx-bch.cryptonermal.net:50002",
                "electrumx-cash.1209k.com:50002",
                "fulcrum.fountainhead.cash:50002",
                "greedyhog.mooo.com:50002"
#else // MASTERNET and TESTNET
                "bch0.kister.net:51002",
                "blackie.c3-soft.com:60002",
                "electroncash.de:50004",
                "tbch.loping.net:60002",
                "testnet.bitcoincash.network:60002",
                "testnet.imaginary.cash:50002",
#endif
            };

            SetElectrumConnectionOptions(electrumSettings);
        }
    };
} //namespace beam::bitcoin_cash
