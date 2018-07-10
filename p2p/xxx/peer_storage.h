#pragma once
#include "peer_info.h"
#include "utility/io/errorhandling.h"
#include <unordered_map>
#include <stdio.h>

namespace beam {

class PeerStorage {
public:
    using LoadCallback = std::function<void(PeerInfo&)>;

    ~PeerStorage();

    io::Result open(const std::string& fileName);
    io::Result load_peers(const LoadCallback& cb);
    io::Result forget_old_peers(uint32_t howLong);
    io::Result update_peer(const PeerInfo& peer);
    void close();

private:
    FILE* _file=0;

    // peer id -> file offset
    std::unordered_map<SessionId, long> _index;
};

} //namespace
