#pragma once
#include "peer_info.h"
#include "roulette.h"

namespace beam {

/// Connect probability weight and ban status for server peer
struct KnownServerMetrics {
    size_t weight=0;
    bool isBanned=false;

    SERIALIZE(weight, isBanned)
};

/// Known servers container and protocol message
using KnownServers = std::unordered_map<Peer, KnownServerMetrics>;

/// Set of serversto connect to
class Servers {
public:
    explicit Servers(size_t maxWeight);

    /// Returns known servers to be sent to network
    const KnownServers& get_known_servers() const;

    /// Updates servers list received from network or on loading
    void update(const KnownServers& received, bool isInitialLoad);

    /// Chooses random (weighted) peer address to connect to
    Peer get_connect_candidate();

    /// Updated peer metrics as connect candidate
    /// (before possible reconnect or while banning/unbanning)
    void update_connect_candidate(Peer p, double weightCoefficient, bool banned);

    /// Marks peer as banned
    void set_banned(Peer p, bool isBanned);

private:
    void update(Peer p, const KnownServerMetrics& m, bool isInitialLoad);

    KnownServers _allServers;
    std::unordered_set<Peer> _connectCandidates;
    Roulette _connectRoulette;
};

} //namespace
