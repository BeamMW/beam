#pragma once
#include "utility/io/address.h"
#include "utility/serialize_fwd.h"
#include <string>
#include <vector>

namespace beam {

struct P2PSettings {
    std::string thisPeerFile;
    //std::string peersFile;
    //std::string ipsFile;

    uint64_t peerId=0;
    uint32_t bindToIp=0;
    uint16_t listenToPort=0;

    // peers that will be always reconnected
    std::vector<io::Address> priorityPeers;

    // load

    // save

    SERIALIZE(peerId, bindToIp, listenToPort);
};

} //namespace
