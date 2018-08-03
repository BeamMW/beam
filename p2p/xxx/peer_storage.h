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
