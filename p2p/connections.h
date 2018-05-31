#pragma once
#include "peer_info.h"
#include "connection.h"
#include <unordered_map>

namespace beam {

/// Active connections
class Connections {
public:
    Connections(ProtocolBase& protocol);

    uint32_t total_connected() const;

    void on_connected(Peer peerId, Connection::Direction d, io::TcpStream::Ptr&& stream);

    void on_disconnected(Peer peerId);

    io::Result write_msg(Peer peerId, const std::vector<io::SharedBuffer>& fragments);

    void broadcast_msg(const std::vector<io::SharedBuffer>& fragments);

private:
    ProtocolBase& _protocol;
    std::unordered_map<Peer, Connection> _connections;
};

} //namespace
