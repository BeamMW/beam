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
#include <vector>
#include "core/fly_client.h"
#include "wallet/core/exchange_rate.h"
#include "contract_var_reader.h"
#include "interface.h"   // IExchangeRatesObserver

namespace beam::wallet
{
    class ContractRateProvider
    {
    public:
        ContractRateProvider(proto::FlyClient::INetwork::Ptr net, bool active);

        void refresh(Height tip);              // trigger a read cycle (call on new tip); tip enforces median validity
        void setOnOff(bool active);            // called by WalletClient::switchOnOffExchangeRates
        ExchangeRates getRates() const { return m_Rates; }

        void Subscribe(IExchangeRatesObserver* o);
        void Unsubscribe(IExchangeRatesObserver* o);

        // usd-per-whole-coin -> ExchangeRate.m_rate fixed-point (scaled by Rules::Coin). Public + static
        // so the unit test verifies THIS code path, not a re-derived copy of the formula.
        static Amount ToRate(double usdPerWholeCoin);

    private:
        void onMedian(const OracleMedian&);
        void onPools(std::vector<PoolData>&&, double usdPerBeam, Height medianEnd);
        void publish();

        bool m_Active;
        ContractVarReader m_Reader;
        ECC::uintBig m_CidOracle, m_CidAmm;
        Height m_Tip = 0;                      // current tip, for median validity ("blank over wrong")
        ExchangeRates m_Rates;
        std::vector<IExchangeRatesObserver*> m_Observers;
    };
}
