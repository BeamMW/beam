#include "connect_pool.h"
#include "utility/config.h"
#include "utility/logger.h"

namespace beam {

void ConnectPool::setup(PeerId thisId, const std::vector<io::Address>& priorityPeers) {
    _thisId = thisId;
    for (auto a: priorityPeers) {
        set_priority_peer(a);
    }
}

void ConnectPool::set_priority_peer(io::Address address) {
    if (_priorityPeers.insert(address).second) {
        if (!_peersReservedByAddress.count(address)) {
            _priorityPeersUnconnected.push_back(address);
        }
    }
}

const std::vector<io::Address>& ConnectPool::get_connect_candidates() {
    static size_t maxActiveConnections = config().get_int("p2p.max_active_connections", 13, 1, 1000);

    _connectCandidates = std::move(_priorityPeersUnconnected);
    size_t n = _connectCandidates.size() + _peersReservedByAddress.size(); // the latter is # of active connections

    if (n < maxActiveConnections) {
        n = maxActiveConnections - n;

        // TODO pull from roulette
    }

    return _connectCandidates;
}

void ConnectPool::schedule_reconnect(io::Address address, io::ErrorCode whatHappened) {
    // TODO check whatHappened and decide when to reconnect

    LOG_ERROR() << "connect to " << address << " failed, code=" << io::error_str(whatHappened) << ", rescheduling";

    auto it = _peersReservedByAddress.find(address);
    if (it != _peersReservedByAddress.end()) {
        _peersReservedById.erase(it->second);
        _peersReservedByAddress.erase(it);
    }

    if (_priorityPeers.count(address)) {
        _priorityPeersUnconnected.push_back(address);
    } else {
        // push to roulette
    }
}

bool ConnectPool::is_ip_allowed(uint32_t) {
    // TODO ban
    return true;
}

ConnectPool::PeerReserveResult ConnectPool::reserve_peer_id(PeerId id, io::Address address) {
    if (_peersReservedById.count(id) || id == _thisId) {
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
