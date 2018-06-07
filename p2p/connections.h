#pragma once
#include "connection.h"
#include <unordered_map>

namespace beam {

/// Active connections
class Connections {
public:
    using OnConnectionRemoved = std::function<void(uint64_t)>;

    Connections(OnConnectionRemoved removedCallback);

    uint32_t total_connected() const;

    void add_connection(Connection::Ptr&& conn);

    void remove_connection(uint64_t id);

    io::Result write_msg(uint64_t id, const SerializedMsg& fragments);
    io::Result write_msg(uint64_t id, const io::SharedBuffer& msg);

    void broadcast_msg(const SerializedMsg& fragments);
    void broadcast_msg(const io::SharedBuffer& msg);

private:
    using Container = std::unordered_map<uint64_t, Connection::Ptr>;

    bool find(uint64_t id, Container::iterator& it);

    OnConnectionRemoved _removedCallback;
    Container _connections;
    std::vector<uint64_t> _toBeRemoved;
};

} //namespace
