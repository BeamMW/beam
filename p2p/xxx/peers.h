#pragma once
#include "peer_storage.h"
#include "roulette.h"
#include "utility/shared_data.h"
#include <mutex>
#include <map>
#include <unordered_map>
#include <vector>

namespace beam {

/// Known servers storage
class KnownServers {
public:
    KnownServers() {}

    using PeerSet = std::unordered_set<Peer>;

    void add(Peer p, size_t weight) {
        auto& address = _ip2Server[peer_id(p)];
        if (address == 0) {
            address = p;
            // *@&^%(*&^*@&*@*&)
        }

    }

    SERIALIZE(_servers);
private:
    std::unordered_map<Peer, Peer> _ip2Server;
    PeerSet _servers;
    //Roulette _connectRoulette;
};

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

    //void on_peer_connected(io::Address address, uint)

private:
    struct PeerInfoInternal {
        SharedData<PeerInfo> info;
        bool toBeUpdated=false;
    };

    using AllPeers = std::unordered_map<Peer, std::unique_ptr<PeerInfoInternal>>;

    // Address->bannedUntil
    using BannedPeers = std::unordered_map<Peer, Timestamp>;
    using BannedUntil = std::map<Timestamp, Peer>;

    // Address->weight
    using ConnectedPeers = std::unordered_map<Peer, uint32_t>;

    // height->Address
    using ConnectedPeersByHeight = std::map<uint64_t, Peer>;

    // Address -> connect attempt #
    using ConnectingPeers = std::unordered_map<Peer, uint32_t>;

    // add new peer impl
    void add_new_peer(PeerInfo& peer, Timestamp now, bool toBeUpdated);

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
