#include "connection.h"
#include <assert.h>

#include "utility/logger.h"

namespace beam {

Connection::Connection(ProtocolBase& protocol, uint64_t peerId, Connection::Direction d, size_t defaultMsgSize, io::TcpStream::Ptr&& stream) :
    _msgReader(protocol, peerId, defaultMsgSize),
    _direction(d),
    _stream(std::move(stream))
{
    assert(_stream);
    _peerAddress = _stream->peer_address();
    _stream->enable_read([this](io::ErrorCode what, void* data, size_t size){ _msgReader.new_data_from_stream(what, data, size); });
}

Connection::~Connection()
{

}

io::Result Connection::write_msg(const std::vector<io::SharedBuffer>& fragments) {
    return _stream->write(fragments);
}

io::Result Connection::write_msg(const io::SharedBuffer& msg) {
    return _stream->write(msg);
}

void Connection::shutdown() {
    _stream->shutdown();
}

/// Returns socket address (non-null if connected)
io::Address Connection::address() const {
    return _stream->address();
}

/// Returns peer address (non-null if connected)
io::Address Connection::peer_address() const {
    return _peerAddress;
}

} //namespace

