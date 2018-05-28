#include "p2p.h"

namespace beam {

P2P::P2P(io::Address bindTo, uint16_t listenTo) :
    _knownServers(15), // max weight TODO from config
    _protocol(0xAA, 0xBB, 0xCC, 22, *this, 0x2000),
    _bindToIp(bindTo),
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
        io::Address listenTo(_bindToIp, _port);
        _thisServer = io::TcpServer::create(_reactor, listenTo, BIND_THIS_MEMFN(on_stream_accepted));
        LOG_DEBUG() << "Listening to " << listenTo;
    }
    connect_to_servers();
    run_async();
}

void P2P::connect_to_servers() {
    // TODO limit # of connections

    for (;;) {
        io::Address a = _knownServers.get_connect_candidate();
        if (a.empty())
            break;

        // TODO check if banned by IP

        LOG_INFO() << "Connecting to " << a << TRACE(_bindToIp);
        auto result = _reactor->tcp_connect(a, a.u64(), BIND_THIS_MEMFN(on_stream_connected), -1, _bindToIp);
        if (!result) {
            LOG_ERROR() << "Cannot connect to " << a << " error=" << io::error_str(result.error());
        }

    }
}

void P2P::on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode) {
    if (errorCode != 0) {
        LOG_DEBUG() << "accept error, code=" << io::error_str(errorCode);
        return;
    }

    // TODO is ip banned ? if yes, close

    LOG_DEBUG() << "Stream accepted, socket=" << newStream->address() << " peer=" << newStream->peer_address();
    assert(newStream);

    // wait for handshake request

    // TODO nonce is stream ID

}

void P2P::on_stream_connected(Peer peer, io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode) {
    if (errorCode != 0) {
        LOG_DEBUG() << "connect error, code=" << io::error_str(errorCode);
        return;
    }

    LOG_INFO() << "Connected to " << newStream->peer_address() << " socket=" << newStream->address();

    // double check if IP banned

    // send handshake

    // wait for handshake response
}

void P2P::on_protocol_error(Peer from, ProtocolError error) {
    LOG_INFO() << "Protocol error " << error << " from " << io::Address::from_u64(from).str();
}

void P2P::on_connection_error(Peer from, int errorCode) {
    LOG_INFO() << "Connection error from " << io::Address::from_u64(from).str() << " error=" << io::error_str(io::ErrorCode(errorCode));
}

} //namespace
