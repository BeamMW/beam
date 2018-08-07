// Copyright 2018 The Beam Team
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

#include "servers.h"
#include "utility/logger.h"

namespace beam {

Servers::Servers(CommonMessages& commonMessages, RandomGen& rdGen, uint32_t maxWeight) :
    _commonMessages(commonMessages),
    _connectRoulette(rdGen, maxWeight)
{}

const KnownServers& Servers::get_known_servers() const {
    return _allServers;
}

bool Servers::update(const KnownServers& received, bool isInitialLoad) {
    bool listChanged = false;
    for (const auto& [address, weight] : received) {
        uint32_t correctedWeight = isInitialLoad ? weight : 1;
        if (add_server(address, correctedWeight)) listChanged = true;
    }
    return listChanged;
}

bool Servers::add_server(io::Address a, uint32_t weight) {
    bool isNewServer = false;
    uint32_t& w = _allServers[a];
    if (w == 0) {
        isNewServer = true;
        w = weight;
        LOG_INFO() << "New server address=" << a;
        _connectCandidates.insert(a);
        _stateChanged = true;
    }
    return isNewServer;
}

io::Address Servers::get_connect_candidate() {
    if (_allServers.empty()) {
        return io::Address();
    }

    if (!_connectCandidates.empty()) {
        for (auto p : _connectCandidates) {
            _connectRoulette.push(p.u64(), _allServers[p]);
        }
        _connectCandidates.clear();
    }

    return io::Address::from_u64(_connectRoulette.pull());
}

void Servers::update_weight(io::Address p, double weightCoefficient) {
    auto it = _allServers.find(p);
    if (it == _allServers.end()) {
        LOG_WARNING() << "Unknown server " << p.str() << ", ignoring...";
        return;
    }
    _connectCandidates.insert(p);
    if (weightCoefficient != 1.0) {
        uint32_t newWeight = uint32_t(weightCoefficient * it->second);
        it->second = newWeight > 0 ? newWeight : 1;
    }
}

void Servers::update_known_servers_response() {
    if (_stateChanged) {
        _commonMessages.update(KNOWN_SERVERS_RESPONSE_MSG_TYPE, _allServers);
        _stateChanged = false;
    }
}

} //namespace
