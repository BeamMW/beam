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

#include "handshake.h"
#include "protocol.h"
#include "utility/logger.h"

namespace beam {

const char* HandshakeError::str() const {
    switch (what) {
        case duplicate_connection: return "duplicate connection";
        case peer_rejected: return "peer rejected";
        default: break;
    }
    return "unknown";
}

HandshakingPeers::HandshakingPeers(ConnectPool& connectPool, OnPeerHandshaked onHandshaked, OnHandshakeError onError) :
    _connectPool(connectPool),
    _onPeerHandshaked(std::move(onHandshaked)),
    _onError(std::move(onError))
{
    assert(_onPeerHandshaked && _onError);
}

void HandshakingPeers::setup(Protocol& protocol, uint16_t thisNodeListenPort, PeerId thisPeerId) {
    protocol.add_message_handler<HandshakingPeers, Handshake, &HandshakingPeers::on_handshake_message>(HANDSHAKE_MSG_TYPE, this, 2, 20);
    protocol.add_message_handler<HandshakingPeers, HandshakeError, &HandshakingPeers::on_handshake_error>(HANDSHAKE_ERROR_MSG_TYPE, this, 1, 9);

    Handshake hs;
    hs.peerId = thisPeerId;
    hs.listensTo = thisNodeListenPort;
    _message = protocol.serialize(HANDSHAKE_MSG_TYPE, hs, true);
    _duplicateConnectionErrorMsg = protocol.serialize(HANDSHAKE_ERROR_MSG_TYPE, HandshakeError{HandshakeError::duplicate_connection}, true);
    _peerRejectedErrorMsg = protocol.serialize(HANDSHAKE_ERROR_MSG_TYPE, HandshakeError{HandshakeError::peer_rejected}, true);
}

void HandshakingPeers::on_new_connection(Connection::Ptr&& conn) {
    StreamId streamId(conn->id());

    if (_connections.count(streamId)) {
        LOG_WARNING() << "ignoring duplicating connection, address=" << streamId.address();
        return;
    }

    bool isOutbound = conn->direction() == Connection::outbound;
    streamId.fields.flags = StreamId::handshaking | (isOutbound ? StreamId::outbound : StreamId::inbound);
    conn->change_id(streamId.u64);
    if (isOutbound) {
        // sending handshake
        auto result = conn->write_msg(_message);
        if (!result) {
            LOG_WARNING() << "cannot send handshake request to " << conn->peer_address() << "error=" << io::error_str(result.error());
            return;
        }
    }

    // wait for handshake messages **only**
    conn->disable_all_msg_types();
    conn->enable_msg_type(HANDSHAKE_MSG_TYPE);
    if (isOutbound) conn->enable_msg_type(HANDSHAKE_ERROR_MSG_TYPE);

    _connections[streamId] = std::move(conn);
}

void HandshakingPeers::on_disconnected(StreamId streamId) {
    _connections.erase(streamId);
}

bool HandshakingPeers::on_handshake_message(uint64_t id, Handshake&& hs) {
    StreamId streamId(id);

    Connections::iterator it = _connections.find(streamId);

    if (it == _connections.end()) {
        LOG_WARNING() << "wrong stream id, address=" << streamId.address();
        return false;
    }

    Connection::Ptr conn = std::move(it->second);
    _connections.erase(it);

    ConnectPool::PeerReserveResult result = _connectPool.reserve_peer_id(hs.peerId, conn->peer_address());

    bool isInbound = streamId.flags() & StreamId::inbound;
    if (isInbound) {
        if (result == ConnectPool::success) {
            auto r = conn->write_msg(_message);
            if (!r) {
                LOG_WARNING() << "cannot send handshake response to " << streamId.address() << " error=" << io::error_str(r.error());
                return false;
            }
        } else if (result == ConnectPool::peer_exists) {
            conn->write_msg(_duplicateConnectionErrorMsg);
            conn->shutdown();
            return false;
        } else if (result == ConnectPool::peer_banned) {
            conn->write_msg(_peerRejectedErrorMsg);
            conn->shutdown();
            return false;
        } else {
            assert(false && "unexpected reserve peer result");
            return false;
        }
    } else if (result != ConnectPool::success) {
        return false;
    }

    LOG_INFO() << "handshake succeeded with peer=" << conn->peer_address() << TRACE(hs.listensTo);
    streamId.fields.flags &= ~StreamId::handshaking;
    if (hs.listensTo) {
        // stream ids for nodes that listen reflects listening port
        streamId.fields.port = hs.listensTo;
    }
    conn->change_id(streamId.u64);
    _onPeerHandshaked(std::move(conn), (hs.listensTo != 0));
    return true;
}

bool HandshakingPeers::on_handshake_error(uint64_t id, HandshakeError&& hs) {
    StreamId streamId(id);

    if ((streamId.flags() & (StreamId::handshaking | StreamId::outbound)) == 0) {
        LOG_DEBUG() << "wrong flags, streamId=" << std::hex << streamId.u64 << std::dec;
    }

    auto it = _connections.find(streamId);
    if (it == _connections.end()) {
        LOG_WARNING() << "handshaking connection is missing for " << streamId.address();
        return false;
    }
    _connections.erase(it);
    _onError(streamId, hs);
    return false;
}

} //namespace
