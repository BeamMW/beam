#pragma once
#include "peer_storage.h"
#include "roulette.h"
#include "utility/shared_data.h"
#include <mutex>
#include <map>
#include <unordered_map>
#include <vector>

namespace beam {

/// Peer management
class Peers {
public:
    Peers();
    ~Peers();
   
    /// Adds an unknown peer w/o statistics. Returns false if it's known by the moment
    bool add_new_peer(io::Address address);
    
    /// Get connected peer IDs sorted by height desc, up to N items
    void get_connected_peers(size_t N, std::vector<std::pair<Peer, Height>>& peers);
    
    /// Returns read access to peer info
    SharedData<PeerInfo>::Reader get_peer_info(Peer peer);
    
private:
    struct PeerInfoInternal {
        SharedData<PeerInfo> info;
        bool toBeUpdated=false;
    };
    
    using AllPeers = std::unordered_map<Peer, PeerInfoInternal>;
    
    // Address->bannedUntil
    using BannedPeers = std::unordered_map<Peer, Timestamp>;
    using BannedUntil = std::map<Timestamp, Peer>;
    
    // Address->weight
    using ConnectedPeers = std::unordered_map<Peer, uint32_t>;
    
    // height->Address
    using ConnectedPeersByHeight = std::map<uint64_t, Peer>;
    
    // Address -> connect attempt #
    using ConnectingPeers = std::unordered_map<Peer, uint32_t>;
            
    std::mutex _mutex;
    AllPeers _allPeers;
    BannedPeers _bannedPeers;
    BannedUntil _bannedUntil;
    ConnectedPeers _connectedPeers;
    ConnectedPeersByHeight _connectedPeersByHeight;
    ConnectingPeers _connectingPeers;
    std::unordered_set<uint64_t> _nonces;
    Handshake _handshake;
    char _handshakeSerialized[64];
    char _pingPongSerialized[64];
    bool _handshakeUpdated=true;
    bool _pingPongUpdated=true;
    KnownServers _knownServers;
    Roulette _connectRoulette;
    PeerStorage _storage;
};

} //namespace
