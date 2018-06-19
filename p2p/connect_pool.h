#pragma once
#include "types.h"
#include <vector>
#include <unordered_set>
#include <unordered_map>

#include "utility/io/errorhandling.h"

namespace beam {

class ConnectPool {
public:
    void setup(PeerId thisId, const std::vector<io::Address>& priorityPeers);

    void set_priority_peer(io::Address addr);

    /// Chooses peers to connect
    const std::vector<io::Address>& get_connect_candidates();

    void schedule_reconnect(io::Address address, io::ErrorCode whatHappened);

    bool is_ip_allowed(uint32_t ip);

    enum PeerReserveResult { success, peer_banned, peer_exists };

    /// returns false if id is already there
    PeerReserveResult reserve_peer_id(PeerId id, io::Address address);

    void release_peer_id(io::Address address);

private:
    PeerId _thisId;
    std::vector<io::Address> _connectCandidates;
    std::unordered_set<io::Address> _priorityPeers;
    std::vector<io::Address> _priorityPeersUnconnected;
    std::unordered_map<PeerId, io::Address> _peersReservedById;
    std::unordered_map<io::Address, PeerId> _peersReservedByAddress;
};

} //namespace
