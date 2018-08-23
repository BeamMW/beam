// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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

    uint32_t pulsePeriodMsec=1000;

    // load

    // save

    SERIALIZE(peerId, bindToIp, listenToPort);
};

} //namespace
