#pragma once
#include "connect_pool.h"
#include "connection.h"

namespace beam {

class Protocol;

class HandshakingPeers {
public:
    using OnPeerHandshaked = std::function<void(Connection::Ptr&& conn, bool isServer)>;
    using OnHandshakeError = std::function<void(StreamId streamId, const HandshakeError& e)>;

    HandshakingPeers(ConnectPool& connectPool, OnPeerHandshaked onHandshaked, OnHandshakeError onError);

    void setup(Protocol& protocol, uint16_t thisNodeListenPort, PeerId thisPeerId);

    void on_new_connection(Connection::Ptr&& conn);

    void on_disconnected(StreamId streamId);

    /// Handler for handshake messages
    bool on_handshake_message(uint64_t id, Handshake&& hs);

    /// Handler for handshake responses from outbound peers
    bool on_handshake_error(uint64_t id, HandshakeError&& hs);

private:
    ConnectPool& _connectPool;

    /// Callbacks
    OnPeerHandshaked _onPeerHandshaked;
    OnHandshakeError _onError;

    using Connections = std::unordered_map<StreamId, Connection::Ptr>;

    /// Connections waiting for handshake request or response
    Connections _connections;

    /// Serialized messages
    io::SharedBuffer _message;
    io::SharedBuffer _duplicateConnectionErrorMsg;
    io::SharedBuffer _peerRejectedErrorMsg;
};

} //namespace
