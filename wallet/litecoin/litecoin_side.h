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

#include "wallet/bitcoin/bitcoin_side.h"
#include "settings_provider.h"

namespace beam::wallet
{
    class LitecoinSide : public BitcoinSide
    {
    public:
        LitecoinSide(BaseTransaction& tx, bitcoin::IBridge::Ptr bitcoinBridge, litecoin::ISettingsProvider& settingsProvider, bool isBeamSide);

        static bool CheckAmount(Amount amount, Amount feeRate);

    protected:

        uint32_t GetLockTxEstimatedTimeInBeamBlocks() const override;
        uint8_t GetAddressVersion() const override;
    };
}