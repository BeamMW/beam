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
            // TODO: uncomment after tests
            //constexpr uint32_t kLTCDefaultLockTimeInBlocks = 2 * 24 * 4 * 6;
            //SetLockTimeInBlocks(kLTCDefaultLockTimeInBlocks);
            SetMinFeeRate(90000);
        }
    };
} //namespace beam::litecoin