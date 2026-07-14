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

#include "contract_rate_provider.h"
#include "asset_price_engine.h"
#include "core/block_crypt.h"   // beam::getTimestamp(), beam::Rules::Coin
#include <algorithm>
#include <cmath>
#include <limits>

namespace beam::wallet
{
    ContractRateProvider::ContractRateProvider(proto::FlyClient::INetwork::Ptr net, bool active)
        : m_Active(active), m_Reader(net)
    {
        m_CidOracle.Scan("4f160f01dcc6751e61d793279b803328d5332125fe8492e93ee8f3bfe9abe13b");
        m_CidAmm.Scan("729fe098d9fd2b57705db1a05a74103dd4b891f535aef2ae69b47bcfdeef9cbf");
    }

    void ContractRateProvider::Subscribe(IExchangeRatesObserver* o) { m_Observers.push_back(o); }
    void ContractRateProvider::Unsubscribe(IExchangeRatesObserver* o)
    {
        m_Observers.erase(std::remove(m_Observers.begin(), m_Observers.end(), o), m_Observers.end());
    }

    void ContractRateProvider::refresh(Height tip)
    {
        m_Tip = tip;                            // record unconditionally so re-enabling has a tip to read at
        if (!m_Active) return;
        if (!m_Rates.empty() && m_CacheEnd && m_CacheEnd < tip)  // cached median expired on this tip advance
        {
            m_Rates.clear();
            m_CacheEnd = 0;
            publish();
        }
        const uint64_t gen = ++m_Gen;           // new cycle; older in-flight callbacks become no-ops
        m_Reader.ReadMedian(m_CidOracle, [this, gen](const OracleMedian& om) { if (gen == m_Gen && m_Active) onMedian(om, gen); });
    }

    void ContractRateProvider::onMedian(const OracleMedian& om, uint64_t gen)
    {
        // "blank over wrong": require a parsed, non-zero, NON-EXPIRED median (m_hEnd >= current tip).
        if (!om.m_Ok || om.m_UsdPerBeam <= 0.0 || om.m_hEnd < m_Tip)
        {
            m_Rates.clear();
            m_CacheEnd = 0;
            publish();
            return;
        }
        double usd = om.m_UsdPerBeam;
        Height hEnd = om.m_hEnd;
        m_Reader.ReadPools(m_CidAmm, [this, usd, hEnd, gen](std::vector<PoolData>&& pools) { if (gen == m_Gen && m_Active) onPools(std::move(pools), usd, hEnd, gen); });
    }

    void ContractRateProvider::onPools(std::vector<PoolData>&& pools, double usdPerBeam, Height medianEnd, uint64_t gen)
    {
        if (gen != m_Gen || !m_Active) return;  // superseded by a newer cycle or turned off while reading pools

        if (medianEnd < m_Tip)   // median expired while we were reading pools -> blank, not a stale price
        {
            m_Rates.clear();
            m_CacheEnd = 0;
            publish();
            return;
        }

        auto priced = AssetPriceEngine::Derive(usdPerBeam, pools);
        ExchangeRates rates;
        const Timestamp now = getTimestamp();
        for (const auto& kv : priced)
        {
            ExchangeRate r;
            r.m_from = Currency(kv.first);          // aid 0 -> BEAM
            r.m_to = Currency::USD();
            r.m_rate = ToRate(kv.second);
            r.m_updateTime = now;
            rates.push_back(std::move(r));
        }
        m_Rates = std::move(rates);
        m_CacheEnd = medianEnd;                 // remember validity so refresh() can expire it on tip advance
        publish();
    }

    Amount ContractRateProvider::ToRate(double usdPerWholeCoin)
    {
        if (!std::isfinite(usdPerWholeCoin) || usdPerWholeCoin <= 0.0) return 0;
        const double scaled = usdPerWholeCoin * double(Rules::Coin);
        // Bound on std::llround's return type (long long), not Amount(uint64): a value in
        // [LLONG_MAX, UINT64_MAX) would pass an Amount-max check yet overflow llround -> UB.
        if (!std::isfinite(scaled) || scaled >= double(std::numeric_limits<long long>::max())) return 0;
        return Amount(std::llround(scaled));
    }

    void ContractRateProvider::setOnOff(bool active)
    {
        if (active == m_Active) return;         // edge-guarded: only act on an actual transition
        m_Active = active;
        ++m_Gen;                                // invalidate any in-flight callbacks
        if (!active)              // off -> don't keep serving a frozen (aging) rate set
        {
            m_Rates.clear();
            m_CacheEnd = 0;
            publish();
            return;
        }
        if (m_Tip)               // on -> read immediately instead of waiting for the next block
        {
            const uint64_t gen = m_Gen;
            m_Reader.ReadMedian(m_CidOracle, [this, gen](const OracleMedian& om) { if (gen == m_Gen && m_Active) onMedian(om, gen); });
        }
    }

    void ContractRateProvider::publish()
    {
        for (auto* o : m_Observers) o->onExchangeRates(m_Rates);
    }
}
