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
#include "wallet/client/extensions/news_channels/contract_price_parse.h"
#include "wallet/client/extensions/news_channels/contract_rate_provider.h"
#include "core/block_crypt.h"
#include <cmath>
#include <cstring>
#include <limits>

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

// F3: when an asset is reachable via two directly-priced hubs, the winning hop must be chosen by
// USD-normalized value (raw hub-side reserve * hub whole-coin USD price), not by raw reserve count.
// A shallow-by-value hub with a high raw reserve count must NOT beat a deep-by-value hub.
void TestAssetPriceEngineHopValue()
{
    const double usd = 0.01;   // usd per BEAM
    std::vector<PoolData> pools = {
        {0, 7,  1000000000000ull, 1000000000000ull},     // direct BEAM/7 -> price(7) == usd (0.01)
        {0, 8,  1000000000000ull, 100000000000000ull},   // direct BEAM/8 -> price(8) == usd/100 (0.0001)
        {7, 50, 1000ull,          250ull},               // 50 via hub 7: hub-per-50=4, hopValue=1000*0.01=10  (WINS)
        {8, 50, 5000ull,          250ull},               // 50 via hub 8: hub-per-50=20, hopValue=5000*0.0001=0.5
                                                         //   (higher RAW count 5000 -> the old bug would pick this)
    };
    auto m = AssetPriceEngine::Derive(usd, pools);

    WALLET_CHECK(approx(m[7], usd));                     // 0.01
    WALLET_CHECK(approx(m[8], usd * 0.01));              // 0.0001
    WALLET_CHECK(approx(m[50], 4.0 * usd));              // 0.04 via hub 7 (USD-value hop wins)
    WALLET_CHECK(!approx(m[50], 20.0 * (usd * 0.01)));   // NOT 0.002 via hub 8 (raw-reserve answer rejected)
}

void TestContractPriceParsers()
{
    // Median value: num=9534975028043687912, order=-70 -> 0.008076438 ; hEnd=3941929
    uint8_t med[24]; memset(med, 0, sizeof(med));
    uint64_t num = 9534975028043687912ull; int32_t order = -70; uint64_t hEnd = 3941929ull;
    memcpy(med + 0, &num, 8); memcpy(med + 8, &order, 4);
    med[12] = 0xAA; med[13] = 0xBB; med[14] = 0xCC; med[15] = 0xDD;   // garbage padding must be ignored
    memcpy(med + 16, &hEnd, 8);
    OracleMedian om = ParseOracleMedianValue(med, sizeof(med));
    WALLET_CHECK(om.m_Ok);
    WALLET_CHECK(std::fabs(om.m_UsdPerBeam - 0.008076438) < 1e-7);
    WALLET_CHECK(om.m_hEnd == 3941929ull);
    WALLET_CHECK(!ParseOracleMedianValue(med, 20).m_Ok);             // short buffer -> not ok

    // Pool key = cid(32) || KeyTag::Internal(0x00) || Amm::Tags::s_Pool(0x01) || aid1(4 LE) || aid2(4 LE) || FeeSettings(1);
    // 43 bytes exactly. val = r1(8 LE) r2(8 LE). aid1 < aid2 (well-ordered, distinct).
    uint8_t key[43]; memset(key, 0, sizeof(key)); key[32] = 0x00; key[33] = 0x01;
    uint32_t a1 = 0, a2 = 3; memcpy(key + 34, &a1, 4); memcpy(key + 38, &a2, 4);
    key[42] = 0x05;   // trailing FeeSettings/kind byte (value irrelevant to parsing)
    uint8_t val[16]; uint64_t r1 = 188925646570ull, r2 = 23340437325353ull;
    memcpy(val, &r1, 8); memcpy(val + 8, &r2, 8);
    PoolData pd;
    WALLET_CHECK(ParsePool(key, sizeof(key), val, sizeof(val), pd));
    WALLET_CHECK(pd.m_Aid1 == 0 && pd.m_Aid2 == 3 && pd.m_Reserve1 == r1 && pd.m_Reserve2 == r2);
    WALLET_CHECK(!ParsePool(key, 42, val, sizeof(val), pd));         // truncated key (no fee byte) -> rejected
    { uint8_t k2[43]; memcpy(k2, key, 43); k2[33] = 0x02;            // wrong ABI tag -> rejected
      WALLET_CHECK(!ParsePool(k2, sizeof(k2), val, sizeof(val), pd)); }
    { uint8_t k2[43]; memcpy(k2, key, 43); uint32_t bad1 = 5, bad2 = 3;
      memcpy(k2 + 34, &bad1, 4); memcpy(k2 + 38, &bad2, 4);          // aid1 >= aid2 -> rejected
      WALLET_CHECK(!ParsePool(k2, sizeof(k2), val, sizeof(val), pd)); }

    // F13: an over-range Float order overflows ldexp to +inf; must fall to blank (0.0),
    // never propagating a non-finite value to the downstream llround.
    uint8_t medBig[24]; memset(medBig, 0, sizeof(medBig));
    uint64_t numBig = 0x8000000000000000ull; int32_t orderBig = 1000; uint64_t hEndBig = 42ull;
    memcpy(medBig + 0, &numBig, 8); memcpy(medBig + 8, &orderBig, 4); memcpy(medBig + 16, &hEndBig, 8);
    OracleMedian omBig = ParseOracleMedianValue(medBig, sizeof(medBig));
    WALLET_CHECK(omBig.m_Ok);                                        // shape is valid -> parse ok
    WALLET_CHECK(omBig.m_UsdPerBeam == 0.0);                         // +inf -> blank over wrong
}

void TestRateScaling()
{
    // ExchangeRate.m_rate == round(usdPerBeam * Rules::Coin); convertAmount() divides rate/Coin.
    WALLET_CHECK(ContractRateProvider::ToRate(0.008076438) == 807644);   // 0.008076438 * Rules::Coin(1e8)

    // F13: non-finite / non-positive inputs must scale to 0, never reaching llround with inf/NaN.
    WALLET_CHECK(ContractRateProvider::ToRate(std::numeric_limits<double>::infinity()) == 0);
    WALLET_CHECK(ContractRateProvider::ToRate(-1.0) == 0);
    WALLET_CHECK(ContractRateProvider::ToRate(0.0) == 0);
    // F13 (llround band): scaled in [LLONG_MAX, UINT64_MAX) must be rejected, not passed to
    // llround (UB). usdPerWholeCoin=9.5e10 -> scaled=9.5e18 > LLONG_MAX(9.22e18) -> 0.
    WALLET_CHECK(ContractRateProvider::ToRate(9.5e10) == 0);
}

thread_local const beam::Rules* beam::Rules::s_pInstance = nullptr;

int main()
{
    TestAssetPriceEngine();
    TestAssetPriceEngineHops();
    TestAssetPriceEngineHopValue();
    TestContractPriceParsers();
    TestRateScaling();
    return WALLET_CHECK_RESULT;
}
