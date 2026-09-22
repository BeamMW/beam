// Copyright 2026 The Beam Team
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
#include <map>
#include <vector>
#include "contract_price_types.h"

namespace beam::wallet
{
    struct AssetPriceEngine
    {
        struct Config
        {
            double m_LiqFloorBeam;   // min BEAM-side reserve (whole BEAM) to trust a pool

            Config(double liqFloorBeam = 1000.0) : m_LiqFloorBeam(liqFloorBeam) {}
        };

        // Returns whole-coin USD price per asset id. Always contains BEAM (aid 0).
        // Assets reachable only through sub-floor pools are omitted.
        static std::map<Asset::ID, double> Derive(
            double usdPerBeam,
            const std::vector<PoolData>& pools,
            const Config& cfg = {});
    };
}
