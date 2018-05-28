#pragma once
#include "peer_info.h"
#include "connection.h"

namespace beam {

using Nonce = uint64_t;

using ConnectionPtr = std::unique_ptr<Connection>;

/// Handshake request/response
struct Handshake {
    enum What { handshake, protocol_mismatch, nonce_exists };

    int what=handshake;
    uint16_t listensTo=0; // if !=0 then this node is a server listening to this port
    uint64_t nonce=0;

    SERIALIZE(what, listensTo, nonce);
};

class HandshakingPeers {
public:
    using OnPeerHandshaked = std::function<void(ConnectionPtr&& conn, uint16_t listensTo)>;

    HandshakingPeers(OnPeerHandshaked callback, io::SharedBuffer serializedHandshake, Nonce thisNodeNonce);

    void connected(uint64_t connId, ConnectionPtr&& conn);

    void disconnected(uint64_t connId);

    /// Handler for handshake requests from inbound peers
    bool on_handshake_request(uint64_t connId, Handshake&& hs);

    /// Handler for handshake responses from outbound peers
    bool on_handshake_response(uint64_t connId, Handshake&& hs);

private:
    /// Callback
    OnPeerHandshaked _onPeerHandshaked;

    /// Serialized handshake from this peer
    io::SharedBuffer _handshake;

    /// Nonce is session ID (at the moment)
    std::unordered_set<Nonce> _nonces;

    using Connections = std::unordered_map<uint64_t, ConnectionPtr>;

    /// Waiting for handshake request
    Connections _inbound;

    /// Waiting for handshake response
    Connections _outbound;
};

} //namespace
