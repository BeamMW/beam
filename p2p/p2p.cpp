#include "p2p.h"

namespace beam {

P2P::P2P(io::Address bindTo, uint16_t listenTo) :
    _knownServers(15), // max weight TODO from config
    _protocol(0xAA, 0xBB, 0xCC, 22, *this, 0x2000),
    _ip(bindTo),
    _port(listenTo)
{
    //protocol.add_message_handler<Request, &NetworkSide::on_request>(requestCode, 1, 2000000);
    //protocol.add_message_handler<Response, &NetworkSide::on_response>(responseCode, 1, 200);
}

P2P::~P2P() {
    stop();
    wait();

}

void P2P::add_known_servers(const KnownServers& servers) {
    _knownServers.update(servers, true);
}

void P2P::start() {
    if (_port != 0) {
        _thisServer = io::TcpServer::create(_reactor, io::Address(_ip, _port), BIND_THIS_MEMFN(on_stream_accepted));
    }
    connect_to_servers();
    run_async();
}

void P2P::connect_to_servers() {
    // TODO limit # of connections

    io::Address a;
    while ((a = _knownServers.get_connect_candidate())) {
        // TODO check if banned by IP

        auto result = _reactor->tcp_connect(a, a.packed, BIND_THIS_MEMFN(on_stream_connected), -1, io::Address(_ip, 0));
        if (!result) {
            LOG_ERROR() << "Cannot connect to " << a.str() << " error=" << io::error_str(result.error());
        }

    }
}

void P2P::on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode) {
    if (errorCode != 0) {
        LOG_DEBUG() << "accept error, code=" << io::error_str(errorCode);
        return;
    }

    // TODO is ip banned ? if yes, close

    LOG_DEBUG() << "Stream accepted, socket=" << newStream->address().str() << " peer=" << newStream->peer_address().str();
    assert(newStream);

    // wait for handshake request

    // TODO nonce is stream ID

}

void P2P::on_stream_connected(Peer peer, io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode) {
    if (errorCode != 0) {
        LOG_DEBUG() << "accept error, code=" << io::error_str(errorCode);
        return;
    }

    LOG_INFO() << "Connection from " << newStream->peer_address().str();

    // double check if IP banned

    // send handshake

    // wait for handshake response
}

void P2P::on_protocol_error(Peer from, ProtocolError error) {
    LOG_INFO() << "Protocol error " << error << " from " << io::Address(from).str();
}

void P2P::on_connection_error(Peer from, int errorCode) {
    LOG_INFO() << "Connection error from " << io::Address(from).str() << " error=" << io::error_str(io::ErrorCode(errorCode));
}

} //namespace
