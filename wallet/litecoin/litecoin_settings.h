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

#include "wallet/bitcoin/bitcoin_settings.h"

namespace beam
{
    using ILitecoindSettingsProvider = IBitcoindSettingsProvider;
    using ILitecoinSettingsProvider = IBitcoinSettingsProvider;
    using LitecoindSettings = BitcoindSettings;

    class LitecoinSettings : public BitcoinSettings
    {
    public:
        LitecoinSettings()
            : BitcoinSettings()
        {
            constexpr uint32_t kLTCDefaultLockTimeInBlocks = 2 * 24 * 4 * 6;
            SetLockTimeInBlocks(kLTCDefaultLockTimeInBlocks);
            SetMinFeeRate(90000);
        }
    };
}