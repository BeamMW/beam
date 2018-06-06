#include "servers.h"
#include "utility/logger.h"

namespace beam {

Servers::Servers(RandomGen& rdGen, uint32_t maxWeight) :
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

} //namespace
