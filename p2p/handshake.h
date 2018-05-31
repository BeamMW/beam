#pragma once
#include "peer_info.h"
#include "protocol_base.h"
#include "connection.h"

namespace beam {

using Nonce = uint64_t;

using ConnectionPtr = std::unique_ptr<Connection>;

/// Handshake request/response
struct Handshake {
    static const MsgType REQUEST_MSG_TYPE = 43;
    static const MsgType RESPONSE_MSG_TYPE = 44;

    uint16_t listensTo=0; // if !=0 then this node is a server listening to this port
    uint64_t nonce=0;

    SERIALIZE(listensTo, nonce);
};

/// Handshake error
struct HandshakeError {
    static const MsgType MSG_TYPE = 45;

    enum { protocol_mismatch = 1, nonce_exists = 2, you_are_banned = 3 };

    int what=0;

    const char* str() const;

    SERIALIZE(what);
};

class Protocol;

class HandshakingPeers {
public:
    using OnPeerHandshaked = std::function<void(ConnectionPtr&& conn, uint16_t listensTo)>;

    HandshakingPeers(Protocol& protocol, OnPeerHandshaked callback, uint16_t thisNodeListenPort, Nonce thisNodeNonce);

    void connected(uint64_t connId, ConnectionPtr&& conn);

    void disconnected(uint64_t connId);

    /// Handler for handshake requests from inbound peers
    bool on_handshake_request(uint64_t connId, Handshake&& hs);

    /// Handler for handshake responses from outbound peers
    bool on_handshake_response(uint64_t connId, Handshake&& hs);

    /// Handler for handshake responses from outbound peers
    bool on_handshake_error_response(uint64_t connId, HandshakeError&& hs);

private:
    /// Callback
    OnPeerHandshaked _onPeerHandshaked;

    /// Serialized handshake and errors from this peer
    io::SharedBuffer _handshakeRequest;
    io::SharedBuffer _handshakeResponse;
    io::SharedBuffer _nonceExistsError;

    /// Nonce is session ID (at the moment)
    std::unordered_set<Nonce> _nonces;

    using Connections = std::unordered_map<uint64_t, ConnectionPtr>;

    /// Waiting for handshake request
    Connections _inbound;

    /// Waiting for handshake response
    Connections _outbound;
};

} //namespace
