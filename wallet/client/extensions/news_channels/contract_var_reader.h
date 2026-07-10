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
#include <functional>
#include <memory>
#include <vector>
#include "core/fly_client.h"
#include "contract_price_types.h"

namespace beam::wallet
{
    // LIFETIME: the reader must be owned for the whole session (a member of ContractRateProvider).
    // FlyClient posts requests async: the request is ref-counted by the network, but the IHandler
    // it calls back into must outlive the round-trip. The reader retains the in-flight handler in a
    // member (m_Median / m_Pools) so it survives until OnComplete fires. A new Read* replaces the
    // prior in-flight one (fine for periodic refresh).
    class ContractVarReader
    {
    public:
        explicit ContractVarReader(proto::FlyClient::INetwork::Ptr net) : m_Net(std::move(net)) {}

        void ReadMedian(const ECC::uintBig& cidOracle, std::function<void(const OracleMedian&)> cb);
        void ReadPools(const ECC::uintBig& cidAmm, std::function<void(std::vector<PoolData>&&)> cb);

    private:
        proto::FlyClient::INetwork::Ptr m_Net;
        std::shared_ptr<proto::FlyClient::Request::IHandler> m_Median;   // in-flight retention
        std::shared_ptr<proto::FlyClient::Request::IHandler> m_Pools;
    };
}
