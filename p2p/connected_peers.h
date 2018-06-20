#pragma once
#include "connection.h"
#include "types.h"
#include <unordered_map>

namespace beam {

class Protocol;

/// Active connections
class ConnectedPeers {
public:
    using OnConnectionRemoved = std::function<void(StreamId)>;

    ConnectedPeers(Protocol& protocol, OnConnectionRemoved removedCallback);

    uint32_t total_connected() const;

    void add_connection(Connection::Ptr&& conn);

    void update_peer_state(StreamId streamId, PeerState&& newState);

    void remove_connection(StreamId id);

    io::Result write_msg(StreamId id, const io::SharedBuffer& msg);

    void ping(const io::SharedBuffer& msg);

    void query_known_servers();

private:
    struct Info {
        Connection::Ptr conn;
        PeerState ps;
        Timestamp lastUpdated=0;
        bool knownServersChanged=false;
    };

    using Container = std::unordered_map<StreamId, Info>;

    void broadcast(const io::SharedBuffer& msg, std::function<bool(Info&)>&& filter);

    bool find(StreamId id, Container::iterator& it);

    OnConnectionRemoved _removedCallback;
    io::SharedBuffer _knownServersQueryMsg;
    Container _connections;
    std::vector<StreamId> _toBeRemoved;
};

} //namespace
