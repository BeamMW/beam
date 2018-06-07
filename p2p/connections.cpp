#include "connections.h"
#include "utility/logger.h"
#include <assert.h>

namespace beam {

Connections::Connections(CommonMessages& commonMessages, Connections::OnConnectionRemoved removedCallback) :
    _commonMessages(commonMessages),
    _removedCallback(std::move(removedCallback))
{
    assert(_removedCallback);
}

uint32_t Connections::total_connected() const {
    return uint32_t(_connections.size());
}

void Connections::add_connection(Connection::Ptr&& conn) {
    assert(conn);

    uint64_t id = conn->id();

    if (_connections.count(id)) {
        LOG_WARNING() << "Connection with id=" << io::Address::from_u64(id) << " already exists, ignoring new connection";
        return;
    }

    Info& i = _connections[id];
    i.conn = std::move(conn);
}

void Connections::remove_connection(uint64_t id) {
    _connections.erase(id);
}

bool Connections::find(uint64_t id, Connections::Container::iterator& it) {
    it = _connections.find(id);
    if (it == _connections.end()) {
        LOG_WARNING() << "Connection with id=" << io::Address::from_u64(id) << " not found";
        return false;
    }
    return true;
}

io::Result Connections::write_msg(uint64_t id, const io::SharedBuffer& msg) {
    Container::iterator it;
    if (!find(id, it)) {
        return make_unexpected(io::EC_ENOTCONN);
    }
    return it->second.conn->write_msg(msg);
}

void Connections::broadcast_msg(const io::SharedBuffer& msg) {
    io::Result result;
    _toBeRemoved.clear();
    for (auto& [id, info] : _connections) {
        result = info.conn->write_msg(msg);
        if (!result) {
            LOG_WARNING() << info.conn->peer_address() << " disconnected, error=" << io::error_str(result.error());
            _toBeRemoved.push_back(id);
        }
    }
    for (auto id : _toBeRemoved) {
        _connections.erase(id);
        _removedCallback(id);
    }
}

void Connections::ping() {
    broadcast_msg(_commonMessages.get(PING_MSG_TYPE));
}

void Connections::pong(uint64_t id) {
    Container::iterator it;
    if (find(id, it)) {
        it->second.conn->write_msg(_commonMessages.get(PONG_MSG_TYPE));
    }
}

void Connections::update_state(uint64_t id, PeerState&& state) {
    Container::iterator it;
    if (!find(id, it)) {
        LOG_WARNING() << "Connection with id=" << io::Address::from_u64(id) << " not found";
        return;
    }
    Info& i = it->second;
    if (i.ps.knownServers != state.knownServers) {
        i.knownServersChanged = true;
    }
    i.ps = std::move(state);
}

void Connections::query_known_servers() {
    io::Result result;
    _toBeRemoved.clear();
    for (auto& [id, info] : _connections) {
        if (info.knownServersChanged) {
            result = info.conn->write_msg(_commonMessages.get(KNOWN_SERVERS_REQUEST_MSG_TYPE));
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
