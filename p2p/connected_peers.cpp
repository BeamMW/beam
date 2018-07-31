#include "connected_peers.h"
#include "protocol.h"
#include "utility/logger.h"

namespace beam {

ConnectedPeers::ConnectedPeers(P2PNotifications& notifications, Protocol& protocol, OnConnectionRemoved removedCallback) :
    _notifications(notifications),
    _removedCallback(removedCallback)
{
    _knownServersQueryMsg = protocol.serialize(KNOWN_SERVERS_REQUEST_MSG_TYPE, VoidMessage{});
}

uint32_t ConnectedPeers::total_connected() const {
    return uint32_t(_connections.size());
}

void ConnectedPeers::add_connection(Connection::Ptr&& conn) {
    assert(conn);

    StreamId id(conn->id());

    if (_connections.count(id)) {
        LOG_WARNING() << "Connection with id=" << id.address() << " already exists, ignoring new connection";
        return;
    }

    Info& i = _connections[id];
    i.conn = std::move(conn);
    _notifications.on_peer_connected(id);
}

void ConnectedPeers::update_peer_state(StreamId streamId, PeerState&& newState) {
    Container::iterator it;
    bool changed = false;
    if (find(streamId, it)) {
        if (newState != it->second.ps) {
            changed = true;
            if (newState.knownServersCount > it->second.ps.knownServersCount) {
                it->second.conn->write_msg(_knownServersQueryMsg);
                it->second.knownServersChanged = true;
            }
            it->second.ps = std::move(newState);
        }
        it->second.lastUpdated = time(0);
    }
    if (changed) _notifications.on_peer_state_updated(streamId, it->second.ps);
}

void ConnectedPeers::remove_connection(StreamId id) {
    _connections.erase(id);
    _notifications.on_peer_disconnected(id);
}

bool ConnectedPeers::find(StreamId id, ConnectedPeers::Container::iterator& it) {
    it = _connections.find(id);
    if (it == _connections.end()) {
        LOG_WARNING() << "connection with not found, " << id.address();
        return false;
    }
    return true;
}

io::Result ConnectedPeers::write_msg(StreamId id, const io::SharedBuffer& msg) {
    Container::iterator it;
    if (!find(id, it)) {
        return make_unexpected(io::EC_ENOTCONN);
    }
    return it->second.conn->write_msg(msg);
}

void ConnectedPeers::broadcast(const io::SharedBuffer& msg, std::function<bool(Info&)>&& filter) {
    io::Result result;
    _toBeRemoved.clear();
    for (auto& [id, info] : _connections) {
        if (filter(info)) {
            result = info.conn->write_msg(msg);
            if (!result) {
                LOG_WARNING() << info.conn->peer_address() << " disconnected, error=" << io::error_str(result.error());
                _toBeRemoved.push_back(id);
            }
        }
    }
    for (auto id : _toBeRemoved) {
        _connections.erase(id);
        _removedCallback(id);
    }
}

void ConnectedPeers::ping(const io::SharedBuffer& msg) {
    //Timestamp now = time(0);
    broadcast(
        msg,
        [/*now*/](ConnectedPeers::Info& i) -> bool {
            return true;

            //static const uint32_t TIMEOUT = 3;
            //Timestamp last = i.lastPingPong;
            //return (now - last >= TIMEOUT);

            //if (now-last >= TIMEOUT) {
            //    i.lastPingPong = now;
            //    return true;
            //}
            //return false;
        }
    );
}

void ConnectedPeers::query_known_servers() {
    io::Result result;
    _toBeRemoved.clear();
    for (auto& [id, info] : _connections) {
        if (info.knownServersChanged) {
            result = info.conn->write_msg(_knownServersQueryMsg);
            if (!result) {
                LOG_WARNING() << info.conn->peer_address() << " disconnected, error=" << io::error_str(result.error());
                _toBeRemoved.push_back(id);
            }
            info.knownServersChanged = false;
        }
    }
    for (auto id : _toBeRemoved) {
        _connections.erase(id);
        _removedCallback(id);
    }
}

} //namespace
