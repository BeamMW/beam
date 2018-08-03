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
#include <unordered_set>
#include <assert.h>

namespace beam {

using SessionId = uint64_t;

struct PeerId {
    union {
        uint64_t u64;
        struct {
            uint32_t ip;
            uint16_t port;
            uint16_t flags;
        } fields;
    };

    io::Address address() const {
        return io::Address(fields.ip, fields.port);
    }

    uint16_t flags() const {
        return fields.flags;
    }

    bool operator==(const PeerId& i) const { return u64 == i.u64; }
    bool operator<(const PeerId& i) const { return u64 < i.u64; }

    PeerId(io::Address a, uint16_t f=0) {
        fields.ip = a.ip();
        fields.port = a.port();
        fields.flags = f;
    }

    PeerId(uint64_t u=0) : u64(u) {}
};

std::ostream& operator<<(std::ostream& os, const PeerId& p);

/// Seconds since the epoch
using Timestamp = uint32_t;

/// Height of connected peer
using Height = uint64_t;

/// Pingpong message, reflects peer's state
struct PeerState {


    Height height=0;
    uint32_t knownServers=0;
    uint32_t connectedPeers=0;

    SERIALIZE(height, knownServers, connectedPeers);
};

/// Peer info
struct PeerInfo {
    SessionId sessionId;
    PeerId lastPeerId;

    // connected state
    uint32_t connectAttempt=0; // >0 for outbound connections
    uint64_t nonce=0; // !=0 if connected
    PeerState state;

    // persistent state
    Timestamp updatedAt=0;
    Timestamp bannedUntil=0; // 0 if not banned
    uint64_t bytesSent=0;
    uint64_t bytesRcvd=0;
    uint32_t nConnects=0;
    uint32_t nFailures=0;

    // weight == relative connect probability
    uint32_t weight;
};

} //namespace
