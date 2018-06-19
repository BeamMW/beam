#include "connected_peers.h"
#include "protocol.h"
#include "utility/logger.h"

namespace beam {

ConnectedPeers::ConnectedPeers(Protocol& protocol, OnConnectionRemoved removedCallback) :
    _removedCallback(removedCallback)
{
    _knownServersQueryMsg = protocol.serialize(KNOWN_SERVERS_REQUEST_MSG_TYPE, VoidMessage{}, true);
}

uint32_t ConnectedPeers::total_connected() const {
    return uint32_t(_connections.size());
}

void ConnectedPeers::add_connection(Connection::Ptr&& conn) {
    assert(conn);

    uint64_t id = conn->id();

    if (_connections.count(id)) {
        LOG_WARNING() << "Connection with id=" << io::Address::from_u64(id) << " already exists, ignoring new connection";
        return;
    }

    Info& i = _connections[id];
    i.conn = std::move(conn);
}

void ConnectedPeers::remove_connection(StreamId id) {
    _connections.erase(id);
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
    Timestamp now = time(0);
    broadcast(
        msg,
        [now](ConnectedPeers::Info& i) -> bool {
            static const uint32_t TIMEOUT = 2;
            Timestamp last = i.lastPingPong;
            i.lastPingPong = now;
            return (now-last >= TIMEOUT);
        }
    );
}

io::Result ConnectedPeers::pong(StreamId id, const io::SharedBuffer& msg) {
    Container::iterator it;
    io::Result result;
    if (find(id, it)) {
        result = it->second.conn->write_msg(msg);
        if (result) {
            it->second.lastPingPong = time(0);
        }
    } else {
        result = make_unexpected(io::EC_ENOTCONN);
    }
    return result;
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
