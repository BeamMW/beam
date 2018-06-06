#pragma once
#include "peer_info.h"
#include "roulette.h"
#include <unordered_set>
#include <unordered_map>

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
    /// Returns true if new servers appeared in list
    bool update(const KnownServers& received, bool isInitialLoad);

    /// Adds a new server address. Returns true if the address is new for the list
    bool add_server(io::Address a, uint32_t weight);

    /// Chooses random (weighted) peer address to connect to
    io::Address get_connect_candidate();

    /// Updated peer metrics as connect candidate
    /// (before possible reconnect)
    void update_weight(io::Address a, double weightCoefficient);

private:
    KnownServers _allServers;
    std::unordered_set<io::Address> _connectCandidates;
    Roulette _connectRoulette;
};

} //namespace
