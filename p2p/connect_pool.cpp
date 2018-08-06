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

#include "connect_pool.h"
#include "utility/config.h"
#include "utility/logger.h"

namespace beam {

ConnectPool::ConnectPool(RandomGen& rdGen) :
    _connectRoulette(rdGen, 4)
{}

void ConnectPool::setup(PeerId thisId, io::Address thisServer, const std::vector<io::Address>& priorityPeers) {
    _thisServer = thisServer;
    reserve_peer_id(thisId, thisServer);
    for (auto a: priorityPeers) {
        set_priority_peer(a);
    }
}

void ConnectPool::set_priority_peer(io::Address address) {
    if (address == _thisServer) return;
    if (_priorityPeers.insert(address).second) {
        if (!_peersReservedByAddress.count(address)) {
            _priorityPeersUnconnected.push_back(address);
        }
    }
}

void ConnectPool::add_connect_candidate(io::Address address, uint32_t weight) {
    if (_peersReservedByAddress.count(address) == 0)
        _otherPeers[address] = weight;
}

const std::vector<io::Address>& ConnectPool::get_connect_candidates() {
    static size_t maxActiveConnections = config().get_int("p2p.max_active_connections", 25, 1, 1000);

    _connectCandidates = std::move(_priorityPeersUnconnected);
    size_t n = _connectCandidates.size() + _peersReservedByAddress.size(); // the latter is # of active connections

    if (n < maxActiveConnections) {
        n = maxActiveConnections - n;

        for (auto& [address, weight] : _otherPeers) {
            _connectRoulette.push(address.u64(), weight);
        }
        _otherPeers.clear();

        io::Address a;

        for (size_t i=0; i<n; ++i) {
            a = io::Address::from_u64(_connectRoulette.pull());
            if (a.empty()) break;
            LOG_DEBUG() << a;
            _connectCandidates.push_back(a);
        }
    }

    return _connectCandidates;
}

void ConnectPool::schedule_reconnect(io::Address address, io::ErrorCode whatHappened) {
    // TODO check whatHappened and decide when to reconnect

    if (address == _thisServer) return;

    LOG_ERROR() << "connection to " << address << " failed, code=" << io::error_str(whatHappened) << ", rescheduling";

    auto it = _peersReservedByAddress.find(address);
    if (it != _peersReservedByAddress.end()) {
        _peersReservedById.erase(it->second);
        _peersReservedByAddress.erase(it);
    }

    if (_priorityPeers.count(address)) {
        _priorityPeersUnconnected.push_back(address);
    } else {
        _otherPeers[address] = 1; //??? TODO flexible weights
    }
}

bool ConnectPool::is_ip_allowed(uint32_t) {
    // TODO ban
    return true;
}

ConnectPool::PeerReserveResult ConnectPool::reserve_peer_id(PeerId id, io::Address address) {
    if (_peersReservedById.count(id)) {
        LOG_ERROR() << "peer id " << std::hex << id << std::dec << " already reserved " << TRACE(address);
        return peer_exists;
    }
    _peersReservedById[id] = address;
    _peersReservedByAddress[address] = id;

    // ban by peer id here

    return success;
}

void ConnectPool::release_peer_id(io::Address address) {
    decltype(_peersReservedByAddress)::iterator it = _peersReservedByAddress.find(address);
    if (it != _peersReservedByAddress.end()) {
        _peersReservedById.erase(it->second);
        _peersReservedByAddress.erase(it);
    }
}

} //namespace
