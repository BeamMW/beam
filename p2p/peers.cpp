#include "peers.h"
#include "utility/config.h"
#include <time.h>

#include "utility/logger.h"

namespace beam {
    
Peers::Peers() :
    _connectRoulette(config().get_int("p2p.max_roulette_weight", 15, 1, 100))
{
    io::Result res = _storage.open(config().get_string("p2p.peers_db_file", "peers.db"));
    if (!res) IO_EXCEPTION(res.error());
    
    //PeerInfo p;
    //PeerInfoInternal peerInfo { SharedData<PeerInfo>(p), false };
        
    uint32_t now = time(0);
    
    PeerStorage::LoadCallback cb = [this, now](PeerInfo& peer) {
        add_new_peer(peer, now, false);
    };
    
    res = _storage.load_peers(cb);
    if (!res) IO_EXCEPTION(res.error());
}

Peers::~Peers() {
    // update all
}

bool Peers::add_new_peer(io::Address address) {
    std::lock_guard<std::mutex> lk(_mutex);
    
    Peer id = peer_id(address);
    if (_allPeers.count(id)) {
        return false;
    }
    
    PeerInfo p;
    p.address = address;
    add_new_peer(p, 0, true);
    
    return true;
}

void Peers::add_new_peer(PeerInfo& peer, Timestamp now, bool toBeUpdated) {
    Peer id = peer_id(peer.address);
        
    Timestamp bannedTs = peer.bannedUntil;
    bool isBanned = false;
        
    if (bannedTs != 0) {
        if (bannedTs > now) {
            _bannedPeers[id] = bannedTs;
            _bannedUntil[bannedTs] = id;
            isBanned = true;
        } else {
            // log that peer unbanned
            peer.bannedUntil = 0;
            toBeUpdated = true;
        }
    }
    
    bool isServer = (!isBanned && peer.address != id); // for servers, port is specified
    
    if (isServer) {
        //TODO _knownServers.servers.insert(peer.address);
        _handshake.state.knownServers++;
        
        // TODO roulette weight algorithm
        if (peer.weight == 0) {
            peer.weight = 1;
            toBeUpdated = true;
        }
        _connectRoulette.push(id, peer.weight);
    }
    
    std::unique_ptr<PeerInfoInternal> ptr = std::make_unique<PeerInfoInternal>();
    *ptr->info.write() = peer;
    ptr->toBeUpdated = toBeUpdated;
        
    LOG_INFO() << "Peer loaded: " << io::Address(peer.address).str() << TRACE(isBanned);
}

void Peers::get_connected_peers(size_t N, std::vector<std::pair<Peer, Height>>& peers) {
    peers.clear();
    
    std::lock_guard<std::mutex> lk(_mutex);
    
    auto it = _connectedPeersByHeight.rbegin();
    auto end = _connectedPeersByHeight.rend();
    for (; N-- && it != end; ++it) {
        peers.push_back({ it->second, it->first });
    }
}

SharedData<PeerInfo>::Reader Peers::get_peer_info(Peer peer) {
    static PeerInfoInternal dummy;
    
    std::lock_guard<std::mutex> lk(_mutex);
    
    auto it = _allPeers.find(peer);
    if (it == _allPeers.end()) {
        return dummy.info.read();
    }
    
    return it->second->info.read();
}
    
} //namespace
