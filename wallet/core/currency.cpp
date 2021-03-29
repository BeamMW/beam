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
#include "currency.h"

namespace beam::wallet {
    const Currency Currency::BEAM    = Currency("beam");
    const Currency Currency::USD     = Currency("usd");
    const Currency Currency::BTC     = Currency("btc");
    const Currency Currency::LTC     = Currency("ltc");
    const Currency Currency::QTUM    = Currency("qtum");
    const Currency Currency::DOGE    = Currency("doge");
    const Currency Currency::DASH    = Currency("dash");
    const Currency Currency::ETH     = Currency("eth");
    const Currency Currency::DAI     = Currency("dai");
    const Currency Currency::USDT    = Currency("usdt");
    const Currency Currency::WBTC    = Currency("wbtc");
    const Currency Currency::BCH     = Currency("bch");
    const Currency Currency::UNKNOWN = Currency("unknown");


}
