#include "handshake.h"
#include "protocol.h"
#include "utility/logger.h"

namespace beam {

const char* HandshakeError::str() const {
    switch (what) {
        case protocol_mismatch: return "protocol mismatch";
        case nonce_exists: return "nonce exists";
        case you_are_banned: return "banned";
        default: break;
    }
    return "unknown";
}

HandshakingPeers::HandshakingPeers(Protocol& protocol, OnPeerHandshaked callback, uint16_t thisNodeListenPort, Nonce thisNodeNonce) :
    _onPeerHandshaked(std::move(callback))
{
    protocol.add_message_handler<HandshakingPeers, Handshake, &HandshakingPeers::on_handshake_request>(Handshake::REQUEST_MSG_TYPE, this, 2, 20);
    protocol.add_message_handler<HandshakingPeers, Handshake, &HandshakingPeers::on_handshake_response>(Handshake::RESPONSE_MSG_TYPE, this, 2, 20);
    protocol.add_message_handler<HandshakingPeers, HandshakeError, &HandshakingPeers::on_handshake_error_response>(HandshakeError::MSG_TYPE, this, 1, 9);
    Handshake hs;
    hs.listensTo = thisNodeListenPort;
    hs.nonce = thisNodeNonce;
    SerializedMsg msg;
    protocol.serialize(msg, Handshake::REQUEST_MSG_TYPE, hs);
    _handshakeRequest = io::normalize(msg);
    protocol.serialize(msg, Handshake::RESPONSE_MSG_TYPE, hs);
    _handshakeResponse = io::normalize(msg);
    HandshakeError hse;
    hse.what = HandshakeError::nonce_exists;
    protocol.serialize(msg, HandshakeError::MSG_TYPE, hse);
    _nonceExistsError = io::normalize(msg);
    _nonces.insert(thisNodeNonce);
}

void HandshakingPeers::connected(uint64_t connId, ConnectionPtr&& conn) {
    if (_inbound.count(connId) || _outbound.count(connId)) {
        LOG_WARNING() << "Ignoring multiple connections to the same IP: " << io::Address::from_u64(connId);
        return;
    }

    bool isInbound = conn->direction() == Connection::inbound;

    if (isInbound) {
        _inbound[connId] = std::move(conn);
    } else {
        auto result = conn->write_msg(_handshakeRequest);
        if (!result) {
            LOG_WARNING() << "Cannot send handshake request to " << conn->peer_address() << "error=" << io::error_str(result.error());
            return;
        }
        _outbound[connId] = std::move(conn);
    }
}

void HandshakingPeers::disconnected(uint64_t connId) {
    if (_inbound.erase(connId) == 0) _outbound.erase(connId);
}

bool HandshakingPeers::on_handshake_request(uint64_t connId, Handshake&& hs) {
    auto it = _inbound.find(connId);
    if (it == _inbound.end()) {
        LOG_WARNING() << "Inbound connections are missing " << io::Address::from_u64(connId);
        return false;
    }

    if (_nonces.count(hs.nonce)) {
        it->second->write_msg(_nonceExistsError);
        it->second->shutdown();
        return false;

        // TODO more errors - flexible protocol version first
    }

    // send response ...
    auto result = it->second->write_msg(_handshakeResponse);
    if (!result) {
        LOG_WARNING() << "Cannot send handshake response to " << it->second->peer_address() << " error=" << io::error_str(result.error());
        return false;
    }

    _onPeerHandshaked(std::move(it->second), hs.listensTo);
    _inbound.erase(it);

    return true;
}

bool HandshakingPeers::on_handshake_response(uint64_t connId, Handshake&& hs) {
    auto it = _outbound.find(connId);
    if (it == _outbound.end()) {
        LOG_WARNING() << "Outbound connections are missing " << io::Address::from_u64(connId);
        return false;
    }

    _onPeerHandshaked(std::move(it->second), hs.listensTo);
    _outbound.erase(it);

    return true;
}

bool HandshakingPeers::on_handshake_error_response(uint64_t connId, HandshakeError&& hs) {
    auto it = _outbound.find(connId);
    if (it == _outbound.end()) {
        LOG_WARNING() << "Outbound connections are missing " << io::Address::from_u64(connId);
        return false;
    }
    LOG_WARNING() << "Unsuccessful handshake response from " << it->second->peer_address() << " reason=" << hs.str();
    _outbound.erase(it);
    return false;
}

} //namespace
