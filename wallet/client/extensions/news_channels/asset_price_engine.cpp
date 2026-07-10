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

#include "asset_price_engine.h"
#include "core/block_crypt.h"   // beam::Rules::Coin

namespace beam::wallet
{
    namespace
    {
        constexpr Asset::ID kBeam = 0;

        // reserve of `from` per 1 unit of `to`, from one pool; 0 if pool doesn't join them.
        double ratioFromPer(const PoolData& p, Asset::ID from, Asset::ID to, double& liqFromSide)
        {
            if (p.m_Aid1 == from && p.m_Aid2 == to) { liqFromSide = double(p.m_Reserve1); return double(p.m_Reserve1) / double(p.m_Reserve2); }
            if (p.m_Aid2 == from && p.m_Aid1 == to) { liqFromSide = double(p.m_Reserve2); return double(p.m_Reserve2) / double(p.m_Reserve1); }
            liqFromSide = 0.0; return 0.0;
        }
    }

    std::map<Asset::ID, double> AssetPriceEngine::Derive(
        double usdPerBeam, const std::vector<PoolData>& pools, const Config& cfg)
    {
        std::map<Asset::ID, double> out;
        if (usdPerBeam <= 0.0)
            return out;                       // no valid oracle -> nothing (blank over wrong)
        out[kBeam] = usdPerBeam;

        const double floorGroth = cfg.m_LiqFloorBeam * double(Rules::Coin);   // BEAM-side reserve floor, in groth

        // Pass 1: direct BEAM pools, deepest wins.
        std::map<Asset::ID, double> bestLiq;
        for (const auto& p : pools)
        {
            if (p.m_Aid1 != kBeam && p.m_Aid2 != kBeam) continue;   // not a BEAM pool
            double liq = 0.0;
            Asset::ID x = (p.m_Aid1 == kBeam) ? p.m_Aid2 : p.m_Aid1;
            double beamPerX = ratioFromPer(p, kBeam, x, liq);
            if (beamPerX <= 0.0 || liq < floorGroth) continue;
            if (out.find(x) == out.end() || liq > bestLiq[x])
            {
                bestLiq[x] = liq;
                out[x] = beamPerX * usdPerBeam;
            }
        }

        // Pass 2: one hop through a DIRECTLY-priced hub, deepest hop pool wins.
        // Read hub prices (and the "already priced" guard) from an immutable snapshot of the pass-1
        // result — NOT the live `out` we write into. Reading live `out` would (a) let a hop-priced
        // asset act as a hub -> silent >1-hop chaining, and (b) skip every later candidate for the
        // same asset after the first write -> "first wins" instead of "deepest wins".
        const std::map<Asset::ID, double> direct = out;
        std::map<Asset::ID, double> bestHopLiq;
        for (const auto& p : pools)
        {
            for (int side = 0; side < 2; ++side)
            {
                Asset::ID x   = side ? p.m_Aid2 : p.m_Aid1;
                Asset::ID hub = side ? p.m_Aid1 : p.m_Aid2;
                if (x == kBeam || hub == kBeam) continue;          // handled by pass 1
                if (direct.find(x) != direct.end()) continue;      // x already has a DIRECT price
                auto itHub = direct.find(hub);
                if (itHub == direct.end()) continue;               // hub must be DIRECTLY priced
                double liq = 0.0;
                double hubPerX = ratioFromPer(p, hub, x, liq);
                if (hubPerX <= 0.0) continue;
                // No liquidity floor on the hop pool (user decision): deepest hop pool wins.
                if (bestHopLiq.find(x) == bestHopLiq.end() || liq > bestHopLiq[x])
                {
                    bestHopLiq[x] = liq;
                    out[x] = hubPerX * itHub->second;
                }
            }
        }
        return out;
    }
}
