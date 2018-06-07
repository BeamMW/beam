#include "connections.h"
#include "utility/logger.h"
#include <assert.h>

namespace beam {

Connections::Connections(Connections::OnConnectionRemoved removedCallback) :
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
        LOG_WARNING() << "Connection with id=" << io::Address::from_u64(id) << " already exists, ignoring nre connection";
        return;
    }

    _connections[id] = std::move(conn);
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

io::Result Connections::write_msg(uint64_t id, const SerializedMsg& fragments) {
    Container::iterator it;
    if (!find(id, it)) {
        return make_unexpected(io::EC_ENOTCONN);
    }
    return it->second->write_msg(fragments);
}

void Connections::broadcast_msg(const io::SharedBuffer& msg) {
    io::Result result;
    _toBeRemoved.clear();
    for (auto& [id, ptr] : _connections) {
        result = ptr->write_msg(msg);
        if (!result) {
            LOG_WARNING() << ptr->peer_address() << " disconnected, error=" << io::error_str(result.error());
            _toBeRemoved.push_back(id);
        }
    }
    for (auto id : _toBeRemoved) {
        _connections.erase(id);
        _removedCallback(id);
    }
}

} //namespace
