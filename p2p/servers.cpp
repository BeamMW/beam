#include "servers.h"
#include "utility/logger.h"

namespace beam {

Servers::Servers(uint32_t maxWeight) :
    _connectRoulette(maxWeight)
{}

const KnownServers& Servers::get_known_servers() const {
    return _allServers;
}

void Servers::update(const KnownServers& received, bool isInitialLoad) {
    for (const auto& [p, m] : received) {
        update(p, m, isInitialLoad);
    }
}

void Servers::update(io::Address p, uint32_t w, bool isInitialLoad) {
    uint32_t& weight = _allServers[p];
    if (weight == 0) {
        LOG_INFO() << "New server address=" << p.str() << TRACE(w);
        if (isInitialLoad) {
            weight = w;
        } else {
            // ignoring metrics if came from network
            weight = 1;
        }
        _connectCandidates.insert(p);
    }
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

void Servers::update_connect_candidate(io::Address p, double weightCoefficient) {
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

} //namespace
