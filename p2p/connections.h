#pragma once
#include "connection.h"
#include "peer_info.h"
#include "common_messages.h"
#include <unordered_map>

namespace beam {

static const MsgType PING_MSG_TYPE = 46;
static const MsgType PONG_MSG_TYPE = 47;
static const MsgType KNOWN_SERVERS_REQUEST_MSG_TYPE = 48;

/// Active connections
class Connections {
public:
    using OnConnectionRemoved = std::function<void(uint64_t)>;

    Connections(CommonMessages& commonMessages, OnConnectionRemoved removedCallback);

    uint32_t total_connected() const;

    void add_connection(Connection::Ptr&& conn);

    void remove_connection(uint64_t id);

    void update_state(uint64_t id, PeerState&& state);

    //io::Result write_msg(uint64_t id, const SerializedMsg& fragments);
    io::Result write_msg(uint64_t id, const io::SharedBuffer& msg);

    //void broadcast_msg(const SerializedMsg& fragments);
    void broadcast_msg(const io::SharedBuffer& msg);

    /// Broadcasts ping across active connections
    void ping();

    void pong(uint64_t id);

    void query_known_servers();

private:
    struct Info {
        Connection::Ptr conn;
        PeerState ps;
        bool knownServersChanged=false;
    };

    using Container = std::unordered_map<uint64_t, Info>;

    bool find(uint64_t id, Container::iterator& it);

    CommonMessages& _commonMessages;
    OnConnectionRemoved _removedCallback;
    Container _connections;
    std::vector<uint64_t> _toBeRemoved;
};

} //namespace
