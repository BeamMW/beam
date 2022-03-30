// Copyright 2020 The Beam Team
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

#include <string>
#include "utility/serialize_fwd.h"
#include "core/block_crypt.h"

namespace beam::wallet
{
    struct Currency
    {
        explicit Currency(std::string val)
            : m_value(std::move(val))
        {
        }

        Currency(const Currency& rhs) = default;

        explicit Currency(beam::Asset::ID assetId);
        [[nodiscard]] beam::Asset::ID toAssetID() const;

        Currency& operator=(const Currency& rhs)
        {
            m_value = rhs.m_value;
            return *this;
        }

        bool operator == (const Currency& rhs) const
        {
            return m_value == rhs.m_value;
        }

        bool operator != (const Currency& rhs) const
        {
            return !operator==(rhs);
        }

        bool operator < (const Currency& rhs) const
        {
            return m_value < rhs.m_value;
        }

        SERIALIZE(m_value);
        static const Currency& UNKNOWN();
        static const Currency& BEAM();
        static const Currency& USD();
        static const Currency& BTC();
        static const Currency& LTC();
        static const Currency& QTUM();
        static const Currency& DOGE();
        static const Currency& DASH();
        static const Currency& ETH();
        static const Currency& DAI();
        static const Currency& USDT();
        static const Currency& WBTC();
        static const Currency& BCH();
        std::string m_value;
    };
}
