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
#include "exchange_rate.h"

namespace beam::wallet
{
    bool ExchangeRate::operator==(const ExchangeRate& other) const
    {
        return from == other.from
            && to   == other.to
            && rate == other.rate
            && updateTime == other.updateTime;
    }

    bool ExchangeRate::operator!=(const ExchangeRate& other) const
    {
        return !(*this == other);
    }

    const std::string ExchangeRate::USD = std::string("usd");
    const std::string ExchangeRate::BTC = std::string("btc");
}
