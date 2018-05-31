#pragma once
#include "utility/io/address.h"
#include "utility/serialize_fwd.h"
#include <unordered_set>
#include <assert.h>

namespace beam {

/// IP only
using Peer = uint64_t;

/// Only 1 connection per IP allowed
inline Peer peer_id(uint64_t x) { return (x & 0xFFFFFFFF0000); }
inline Peer peer_id(io::Address a) { return peer_id(a.u64()); }

/// Seconds since the epoch
using Timestamp = uint32_t;

/// Height of connected peer
using Height = uint64_t;

/// Pingpong message, reflects peer's state
struct PingPong {
    Height height=0;
    uint32_t knownServers=0;
    uint32_t connectedPeers=0;

    SERIALIZE(height, knownServers, connectedPeers);
};

/// Peer info
struct PeerInfo {
    // IP with port, port==0 for inbound connections
    Peer address;

    // connected state
    uint32_t connectAttempt=0; // >0 for outbound connections
    uint64_t nonce=0; // !=0 if connected
    PingPong state;

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

/// Handshake request/response
struct Handshake {
    enum What { handshake, handshake_ok, protocol_mismatch, nonce_exists };

    int what=handshake;
    uint16_t listensTo=0; // if !=0 then this node is a server listening to this port
    uint64_t nonce=0;
    PingPong state;

    SERIALIZE(what, nonce, state);
};

/// Known servers response
struct KnownServers {
    std::unordered_set<Peer> servers;

    SERIALIZE(servers);
};

} //namespace
