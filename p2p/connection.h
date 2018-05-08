#pragma once
#include "msg_reader.h"
#include "utility/io/tcpstream.h"

namespace beam {

/// Reads-writes messages from-to connected stream
class Connection {
public:
    /// Attaches connected tcp stream to protocol
    Connection(ProtocolBase& protocol, uint64_t peerId, size_t defaultMsgSize, io::TcpStream::Ptr&& stream);

    ~Connection();

    /// Writes fragments to stream
    io::Result write_msg(const std::vector<io::SharedBuffer>& fragments);

private:
    /// stream message handler
    void on_recv(io::ErrorCode what, const void* data, size_t size);

    ProtocolBase& _protocol;
    uint64_t _peerId;
    MsgReader _msgReader;
    io::TcpStream::Ptr _stream;
};

} //namespace
