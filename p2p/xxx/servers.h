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
#include "common_messages.h"
#include "roulette.h"
#include <unordered_set>
#include <unordered_map>

namespace beam {

static const MsgType KNOWN_SERVERS_RESPONSE_MSG_TYPE = 49;

/// Known servers container and protocol message
/// Address -> weight
using KnownServers = std::unordered_map<io::Address, uint32_t>;

/// Set of serversto connect to
class Servers {
public:
    Servers(CommonMessages& commonMessages, RandomGen& rdGen, uint32_t maxWeight);

    /// Returns known servers to be sent to network
    const KnownServers& get_known_servers() const;

    /// Updates servers list received from network or on loading
    /// Returns true if new servers appeared in list
    bool update(const KnownServers& received, bool isInitialLoad);

    /// Adds a new server address. Returns true if the address is new for the list
    bool add_server(io::Address a, uint32_t weight);

    /// Chooses random (weighted) peer address to connect to
    io::Address get_connect_candidate();

    /// Updated peer metrics as connect candidate
    /// (before possible reconnect)
    void update_weight(io::Address a, double weightCoefficient);

    /// Updates serialized known servers response if changed
    void update_known_servers_response();

private:
    CommonMessages& _commonMessages;
    bool _stateChanged=false;
    KnownServers _allServers;
    std::unordered_set<io::Address> _connectCandidates;
    Roulette _connectRoulette;
};

} //namespace
