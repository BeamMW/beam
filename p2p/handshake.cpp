#include "handshake.h"
#include "utility/logger.h"

namespace beam {

HandshakingPeers::HandshakingPeers(OnPeerHandshaked callback, io::SharedBuffer serializedHandshake, Nonce thisNodeNonce) :
    _onPeerHandshaked(std::move(callback)),
    _handshake(std::move(serializedHandshake))
{
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
        auto result = conn->write_msg(_handshake);
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

    Handshake::What what = Handshake::handshake;
    if (hs.what != what) {
        what = Handshake::protocol_mismatch;
    } else if (_nonces.count(hs.nonce)) {
        what = Handshake::nonce_exists;
    }

    if (what != Handshake::handshake) {
        //Handshake response;
        // TODO send response response.what =


        // TODO streams shutdown in reactor
        return true;
    }

    // send response ...
    auto result = it->second->write_msg(_handshake);
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

    if (hs.what != Handshake::handshake) {
        LOG_WARNING() << "Unsuccessful handshake response from " << it->second->peer_address();
        return false;
    }

    _onPeerHandshaked(std::move(it->second), hs.listensTo);
    _outbound.erase(it);

    return true;
}

} //namespace
