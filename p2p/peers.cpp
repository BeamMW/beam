#include "peers.h"
#include "utility/config.h"
#include <time.h>


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
        Peer id = peer_id(peer.address);
        
        Timestamp bannedTs = peer.bannedUntil;
        bool isBanned = false;
        bool isDirty = false;
        
        if (bannedTs != 0) {
            if (bannedTs > now) {
                _bannedPeers[id] = bannedTs;
                _bannedUntil[bannedTs] = id;
                isBanned = true;
            } else {
                // log that peer unbanned
                peer.bannedUntil = 0;
                isDirty = true;
            }
        }
        
        bool isServer = (!isBanned && peer.address != id); // for servers, port is specified
        
        if (isServer) {
            _knownServers.servers.insert(peer.address);
            _handshake.state.knownServers++;
            
            // TODO roulette weight algorithm
            if (peer.weight == 0) {
                peer.weight = 1;
                isDirty = true;
            }
            _connectRoulette.push(id, peer.weight);
        }
        
        //_allPeers[id] = PeerInfoInternal { SharedData<PeerInfo>(peer), isDirty };
        
        //SharedData<PeerInfo> i (peer);
        //_allPeers[id].info = SharedData<PeerInfo>(std::move(peer));
        //_allPeers.insert({ id, PeerInfoInternal{ peer, isDirty } });
    };
    
    res = _storage.load_peers(cb);
    if (!res) IO_EXCEPTION(res.error());
    
}

Peers::~Peers() {
    // update all
}
    
} //namespace
