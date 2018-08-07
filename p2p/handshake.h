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
