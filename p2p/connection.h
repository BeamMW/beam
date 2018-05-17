#pragma once
#include "msg_reader.h"
#include "utility/io/tcpstream.h"

namespace beam {

/// Reads-writes messages from-to connected stream
class Connection {
public:
    enum Direction : uint8_t { inbound, outbound };
    
    /// Attaches connected tcp stream to protocol
    Connection(ProtocolBase& protocol, uint64_t peerId, Direction d, size_t defaultMsgSize, io::TcpStream::Ptr&& stream);

    /// Dtor    
    ~Connection();

    /// Writes fragments to stream
    io::Result write_msg(const std::vector<io::SharedBuffer>& fragments);
    
    /// Returns socket address (non-null if connected)
    io::Address address() const;
    
    /// Returns peer address (non-null if connected)
    io::Address peer_address() const;
    
    /// Returns direction
    Direction direction() const { return _direction; }

private:
    /// stream message handler
    void on_recv(io::ErrorCode what, const void* data, size_t size);

    ProtocolBase& _protocol;
    uint64_t _peerId;
    MsgReader _msgReader;
    io::TcpStream::Ptr _stream;
    const Direction _direction;
};

} //namespace
