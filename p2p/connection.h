#pragma once
#include "msg_reader.h"
#include "utility/io/tcpstream.h"

namespace beam {

/// Reads-writes messages from-to connected stream
class Connection {
public:
    enum Direction : uint8_t { inbound, outbound };

    using Ptr = std::unique_ptr<Connection>;

    /// Attaches connected tcp stream to protocol
    Connection(ProtocolBase& protocol, uint64_t peerId, Direction d, size_t defaultMsgSize, io::TcpStream::Ptr&& stream);

    /// Dtor
    ~Connection();

    uint64_t id() const { return _msgReader.id(); }
    void change_id(uint64_t newId) { _msgReader.change_id(newId); }

    /// Allows receiving messages of given type
    void enable_msg_type(MsgType type) { _msgReader.enable_msg_type(type); }

    /// Allows receiving of all msg types
    void enable_all_msg_types() { _msgReader.enable_all_msg_types(); }

    /// Disables receiving messages of given type
    void disable_msg_type(MsgType type) { _msgReader.disable_msg_type(type); }

    /// Disables all messages
    void disable_all_msg_types() { _msgReader.disable_all_msg_types(); }

    /// Writes fragments to stream
    io::Result write_msg(const SerializedMsg& fragments);

    /// Writes single msg to stream
    io::Result write_msg(const io::SharedBuffer& msg);

    /// Shutdowns write side, waits for pending write requests to complete, but on reactor's side
    void shutdown();

    /// Returns socket address (non-null if connected)
    io::Address address() const;

    /// Returns peer address (non-null if connected)
    io::Address peer_address() const;

    /// Returns direction
    Direction direction() const { return _direction; }

private:
    MsgReader _msgReader;
    io::TcpStream::Ptr _stream;
    const Direction _direction;
    io::Address _peerAddress; // keep it after disconnect
};

} //namespace
