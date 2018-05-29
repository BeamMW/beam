#pragma once
#include "peer_info.h"
#include "roulette.h"

namespace beam {

/// Known servers container and protocol message
/// Address -> weight
using KnownServers = std::unordered_map<io::Address, uint32_t>;

/// Set of serversto connect to
class Servers {
public:
    Servers(RandomGen& rdGen, uint32_t maxWeight);

    /// Returns known servers to be sent to network
    const KnownServers& get_known_servers() const;

    /// Updates servers list received from network or on loading
    void update(const KnownServers& received, bool isInitialLoad);

    /// Chooses random (weighted) peer address to connect to
    io::Address get_connect_candidate();

    /// Updated peer metrics as connect candidate
    /// (before possible reconnect)
    void update_connect_candidate(io::Address p, double weightCoefficient);

private:
    void update(io::Address p, uint32_t w, bool isInitialLoad);

    KnownServers _allServers;
    std::unordered_set<io::Address> _connectCandidates;
    Roulette _connectRoulette;
};

} //namespace
