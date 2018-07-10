#pragma once
#include "protocol_base.h"
#include "utility/io/address.h"
#include <unordered_set>

namespace beam {

/// Random self-generated peer id
/// Used to avoid multiple connections between 2 peers as well as self-connections
using PeerId = uint64_t;

/// Seconds since the epoch
using Timestamp = uint32_t;

/// ID of connected stream, consists of address and state
struct StreamId {
    enum Flags {
        handshaking = 8, active = 4, inbound = 2, outbound = 1
    };

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

    bool operator==(const StreamId& i) const { return u64 == i.u64; }
    bool operator<(const StreamId& i) const { return u64 < i.u64; }

    StreamId(io::Address a, uint16_t f=0) {
        fields.ip = a.ip();
        fields.port = a.port();
        fields.flags = f;
    }

    StreamId(uint64_t u=0) : u64(u) {}
};

/// Handshake request/response
struct Handshake {
    PeerId peerId=0;
    uint16_t listensTo=0; // if !=0 then this node is a server listening to this port

    SERIALIZE(peerId, listensTo);
};

/// Handshake error
struct HandshakeError {
    enum { duplicate_connection = 1, peer_rejected = 2 };

    int what=0;

    const char* str() const;

    SERIALIZE(what);
};

/// Pingpong message, reflects peer's state
struct PeerState {
    uint32_t tip=0;
    uint32_t knownServersCount=0;
    uint32_t connectedPeersCount=0;

    bool operator!=(const PeerState& ps) const {
        return tip != ps.tip || knownServersCount != ps.knownServersCount || connectedPeersCount != ps.connectedPeersCount;
    }

    SERIALIZE(tip, knownServersCount, connectedPeersCount);
};

/// Known servers message
using KnownServers = std::unordered_set<io::Address>;

const MsgType HANDSHAKE_MSG_TYPE = 43;
const MsgType HANDSHAKE_ERROR_MSG_TYPE = 44;
const MsgType PEER_STATE_MSG_TYPE = 45;
const MsgType KNOWN_SERVERS_REQUEST_MSG_TYPE = 46;
const MsgType KNOWN_SERVERS_MSG_TYPE = 47;

} //namespace

namespace std {
    template<> struct hash<beam::StreamId> {
        typedef beam::StreamId argument_type;
        typedef std::size_t result_type;

        result_type operator()(const argument_type& a) const noexcept {
            return std::hash<uint64_t>()(a.u64);
        }
    };
}
