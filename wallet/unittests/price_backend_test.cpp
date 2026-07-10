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

// unit tests for the contract price backend (AssetPriceEngine + parsers).
#include "test_helpers.h"
#include "wallet/client/extensions/news_channels/asset_price_engine.h"
#include <cmath>

using namespace beam;
using namespace beam::wallet;

WALLET_TEST_INIT

namespace
{
    bool approx(double a, double b)
    {
        double m = std::max(std::fabs(a), std::fabs(b));
        return (m == 0.0) ? (std::fabs(a - b) < 1e-12) : (std::fabs(a - b) / m < 1e-6);
    }
}

void TestAssetPriceEngine()
{
    const double usd = 0.00770233;   // usd per BEAM
    std::vector<PoolData> pools = {
        {0, 3,   188925646570ull, 23340437325353ull},    // BEAM/3 direct
        {0, 7,   100000000ull,    50000000ull},           // thin BEAM/7 (below floor, ignored)
        {0, 7,   91193641098572ull, 86169560876675ull},   // deep BEAM/7
        {0, 122, 10000000000ull,  10000ull},              // dust (below floor)
        {7, 50,  19731264227ull,  2068200032ull},         // 50 via hub 7 (1 hop)
    };
    auto m = AssetPriceEngine::Derive(usd, pools);

    WALLET_CHECK(approx(m[0], usd));                                             // BEAM (aid 0) == usdPerBeam
    WALLET_CHECK(approx(m[7], (91193641098572.0 / 86169560876675.0) * usd));     // asset 7 uses the deep pool
    WALLET_CHECK(m.find(122) == m.end());                                        // dust-only asset omitted (blank, not wrong)
    WALLET_CHECK(approx(m[50], (19731264227.0 / 2068200032.0) * m[7]));          // asset 50 via 1 hop through hub 7
}

// Exercises the hop-pass edge cases the first test does not: deepest-of-two hop pools wins
// (order-independent), and a hop-only hub does not chain to a second hop.
void TestAssetPriceEngineHops()
{
    const double usd = 0.01;   // usd per BEAM
    std::vector<PoolData> pools = {
        {0, 7,   1000000000000ull, 1000000000000ull},   // BEAM/7 direct -> price(7) == usd
        {7, 60,  100ull,           300ull},              // shallow 7/60 (must LOSE)
        {7, 60,  900000000000ull,  300000000000ull},     // deep 7/60 (must WIN: hub-per-60 = 3)
        {7, 50,  1000000000ull,    1000000000ull},       // 50 via hub 7 (hop-priced; no direct BEAM/50)
        {50, 99, 1000000000ull,    1000000000ull},       // 99's only hub (50) is hop-priced -> must NOT chain
    };
    auto m = AssetPriceEngine::Derive(usd, pools);

    const double price7 = usd;                            // 1e12/1e12 * usd
    WALLET_CHECK(approx(m[7], price7));
    WALLET_CHECK(approx(m[60], 3.0 * price7));            // deepest 7/60 wins (bug would give 100/300 * price7)
    WALLET_CHECK(approx(m[50], price7));                  // hop-priced via 7
    WALLET_CHECK(m.find(99) == m.end());                 // no 2-hop chaining through the hop-priced hub 50
}

int main()
{
    TestAssetPriceEngine();
    TestAssetPriceEngineHops();
    return WALLET_CHECK_RESULT;
}
