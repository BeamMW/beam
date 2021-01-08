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
#include "dex_order.h"

namespace beam::wallet {

    namespace
    {
        const uint32_t kCurrentOfferVer = 2;
    }

    DexOrder::DexOrder()
        : version(0)
    {
    }

     DexOrder::DexOrder(DexOrderID orderId, WalletID sbbsId, uint64_t sbbsKeyIdx, Asset::ID sellCoin, Asset::ID buyCoin, Amount amount)
        : version(kCurrentOfferVer)
        , orderID(orderId)
        , sbbsID(sbbsId)
        , sbbsKeyIDX(sbbsKeyIdx)
        , sellCoin(sellCoin)
        , buyCoin(buyCoin)
        , amount(amount)
        , isMy(true)
     {
     }
}