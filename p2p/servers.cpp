#include "servers.h"
#include "utility/logger.h"

namespace beam {

Servers::Servers(size_t maxWeight) :
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

void Servers::update(Peer p, const KnownServerMetrics& m, bool isInitialLoad) {
    KnownServerMetrics& metrics = _allServers[p];
    if (metrics.weight == 0) {
        LOG_INFO() << "New server address=" << io::Address(p).str() << " w=" << m.weight << " b=" << m.isBanned;
        if (isInitialLoad) {
            metrics = m;
            if (!m.isBanned) _connectRoulette.push(p, m.weight);
        } else {
            // ignoring metrics if came from network
            metrics.weight = 1;
            metrics.isBanned = false;
            _connectCandidates.insert(p);
        }
    }
}

Peer Servers::get_connect_candidate() {
    size_t sz = _allServers.size();
    if (sz == 0) {
        return 0;
    } else if (sz == 1) {
        auto it = _allServers.begin();
        if (it->second.isBanned) return 0;
        else return it->first;
    }

    if (!_connectCandidates.empty()) {
        for (auto p : _connectCandidates) {
            _connectRoulette.push(p, _allServers[p].weight);
        }
        _connectCandidates.clear();
    }

    Peer p = 0;
    for (;;) {
        p = _connectRoulette.pull();
        if (!p || !_allServers[p].isBanned)
            break;
    }
    return p;
}

void Servers::update_connect_candidate(Peer p, double weightCoefficient, bool banned) {
    auto it = _allServers.find(p);
    if (it == _allServers.end()) {
        LOG_WARNING() << "Unknown server " << io::Address(p) << ", ignoring...";
        return;
    }
    KnownServerMetrics& m = it->second;
    if (banned) {
        m.isBanned = true;
        _connectCandidates.erase(p);
    } else {
        _connectCandidates.insert(p);
        if (weightCoefficient != 1.0) {
            size_t newWeight = size_t(weightCoefficient * m.weight);
            m.weight = newWeight > 0 ? newWeight : 1;
        }
    }
}

} //namespace
