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
    const Currency& Currency::BEAM() {
        static const Currency beam("beam");
        return beam;
    }

    const Currency& Currency::USD() {
        static const Currency usd("usd");
        return usd;
    }

    const Currency& Currency::BTC() {
        static const Currency btc("btc");
        return btc;
    }

    const Currency& Currency::LTC() {
        static const Currency ltc("ltc");
        return ltc;
    }

    const Currency& Currency::QTUM() {
        static const Currency qtum("qtum");
        return qtum;
    }

    const Currency& Currency::DOGE() {
        static const Currency doge("doge");
        return doge;
    }

    const Currency& Currency::DASH() {
        static const Currency dash("dash");
        return dash;
    }

    const Currency& Currency::ETH() {
        static const Currency eth("eth");
        return eth;
    }

    const Currency& Currency::DAI() {
        static const Currency dai("dai");
        return dai;
    }

    const Currency& Currency::USDT() {
        static const Currency usdt("usdt");
        return usdt;
    }

    const Currency& Currency::WBTC() {
        static const Currency wbtc("wbtc");
        return wbtc;
    }

    const Currency& Currency::BCH() {
        static const Currency bch("bch");
        return bch;
    }

    const Currency& Currency::UNKNOWN() {
        static const Currency unknown("unknown");
        return unknown;
    }
}
