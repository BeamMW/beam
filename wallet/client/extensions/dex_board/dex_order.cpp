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
#include "boost/uuid/random_generator.hpp"

namespace beam::wallet {

    namespace
    {
        const uint32_t kCurrentOfferVer = 2;
    }

    std::string DexOrderID::to_string() const
    {
        return to_hex(data(), size());
    }

    DexOrderID DexOrderID::generate()
    {
        boost::uuids::uuid uuid = boost::uuids::random_generator()();
        DexOrderID orderId {};
        std::copy(uuid.begin(), uuid.end(), orderId.begin());
        return orderId;
    }

    DexOrderID::DexOrderID(const std::string& hex)
        : std::array<uint8_t, 16>()
    {
        bool allOK = true;
        const auto vec = from_hex(hex, &allOK);

        if (!allOK || vec.size() != size())
        {
            throw std::runtime_error("failed to convert string to DexOrderID");
        }

        std::copy_n(vec.begin(), size(), begin());
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