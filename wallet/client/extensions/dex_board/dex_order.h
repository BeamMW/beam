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
#include "wallet/core/common.h"

namespace beam::wallet
{
    class DexOrder
    {
    public:
        static uint32_t getCurrentVersion();
        DexOrder();

        // TODO:DEX anything better than walletID?
        DexOrder(DexOrderID orderId, WalletID sbbsId, uint64_t sbbsKeyIdx, Asset::ID sellCoin, Asset::ID buyCoin, Amount amount, time_t expiration);

        // TODO:DEX check version
        // TODO:DEX check that error is generated if any field is missing
        // TODO:DEX check that error is generated if bad version and nothing more is parsed
        // TODO:DEX any exceptions?
        SERIALIZE(version, orderID, sbbsID, sbbsKeyIDX, sellCoin, buyCoin, amount, progress, expiration);

        bool operator==(const DexOrder& other) const
        {
            // TODO:DEX check if this correct & enough
            return orderID == other.orderID;
        }

        [[nodiscard]] bool IsExpired() const;
        [[nodiscard]] bool IsCompleted() const;
        [[nodiscard]] bool CanAccept() const;

        uint32_t   version;
        DexOrderID orderID;     // UUID
        WalletID   sbbsID;      // here wallet listens for order processing
        uint64_t   sbbsKeyIDX;  // index used to generate SBBS key, to identify OUR orders
        Asset::ID  sellCoin = Asset::s_BeamID;
        Asset::ID  buyCoin = Asset::s_BeamID;
        Amount     amount = 0;
        Amount     progress = 0;
        time_t     expiration = 0;
        bool       isMy;
    };
}
