#include "p2p.h"

namespace beam {

P2P::P2P(io::Address bindTo, uint16_t listenTo) :
    _sessionId(_rdGen.rnd<uint64_t>()),
    _knownServers(_rdGen, 15), // max weight TODO from config
    _protocol(0xAA, 0xBB, 0xCC, 100, *this, 0x2000),
    _handshakingPeers(_protocol, BIND_THIS_MEMFN(on_peer_handshaked), listenTo, _sessionId),
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
        LOG_DEBUG() << "Listening to " << listenTo  << TRACE(_sessionId);
    }
    connect_to_servers();
    _timer = set_timer(1000, BIND_THIS_MEMFN(on_timer));
    run_async();
}

void P2P::connect_to_servers() {
    // TODO limit # of connections

    for (;;) {
        io::Address a = _knownServers.get_connect_candidate();
        if (a.empty())
            break;

        // TODO check if banned by IP
        // _ipAccess.is_ip_allowed(a.ip());

        LOG_INFO() << "Connecting to " << a << TRACE(_bindToIp)  << TRACE(_sessionId);
        auto result = _reactor->tcp_connect(a, a.u64(), BIND_THIS_MEMFN(on_stream_connected), -1, _bindToIp);
        if (!result) {
            LOG_ERROR() << "Cannot connect to " << a << " error=" << io::error_str(result.error())  << TRACE(_sessionId);
        }

    }
}

void P2P::on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode) {
    if (errorCode != 0) {
        LOG_DEBUG() << "accept error, code=" << io::error_str(errorCode) << TRACE(_sessionId);
        return;
    }

    // TODO is ip banned ? if yes, close
    // _ipAccess.is_ip_allowed(a.ip());

    LOG_DEBUG() << "Stream accepted, socket=" << newStream->address() << " peer=" << newStream->peer_address() << TRACE(_sessionId);
    assert(newStream);

    // port is ignored for inbound connections ids
    uint64_t id = newStream->peer_address().port(0).u64();

    ConnectionPtr conn = std::make_unique<Connection>(
        _protocol,
        id,
        Connection::inbound,
        10000, //TODO config
        std::move(newStream)
    );

    // wait for handshake request **only**
    conn->disable_all_msg_types();
    conn->enable_msg_type(Handshake::REQUEST_MSG_TYPE);

    _handshakingPeers.connected(id, std::move(conn));

    // TODO nonce is stream ID ????
}

void P2P::on_stream_connected(Peer peer, io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode) {
    if (errorCode != 0) {
        LOG_DEBUG() << "connect error, code=" << io::error_str(errorCode) << TRACE(_sessionId);;
        return;
    }

    LOG_INFO() << "Connected to " << newStream->peer_address() << " socket=" << newStream->address() << TRACE(_sessionId);;

    // double check if IP banned
    // _ipAccess.is_ip_allowed(a.ip());

    ConnectionPtr conn = std::make_unique<Connection>(
        _protocol,
        peer,
        Connection::outbound,
        10000, //TODO config
        std::move(newStream)
    );

    // wait for handshake responses **only**
    conn->disable_all_msg_types();
    conn->enable_msg_type(Handshake::RESPONSE_MSG_TYPE);
    conn->enable_msg_type(HandshakeError::MSG_TYPE);

    // send handshake and wait for handshake response
    _handshakingPeers.connected(peer, std::move(conn));
}

void P2P::on_protocol_error(Peer from, ProtocolError error) {
    LOG_INFO() << "Protocol error " << error << " from " << io::Address::from_u64(from).str() << TRACE(_sessionId);;
}

void P2P::on_connection_error(Peer from, int errorCode) {
    LOG_INFO() << "Connection error from " << io::Address::from_u64(from).str() << " error=" << io::error_str(io::ErrorCode(errorCode)) << TRACE(_sessionId);;
}

void P2P::on_peer_handshaked(ConnectionPtr&& conn, uint16_t listensTo) {
    LOG_INFO() << "Peer handshaked, " << conn->peer_address() << TRACE(_sessionId);;
    if (listensTo) {
        //_knownServers.add();
    }
}

void P2P::on_timer() {
    LOG_DEBUG() << "on timer" << TRACE(_sessionId);
}

} //namespace
