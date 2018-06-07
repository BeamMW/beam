#include "p2p.h"
#include "utility/config.h"
#include "utility/logger.h"

namespace beam {

P2P::P2P(uint64_t sessionId, io::Address bindTo, uint16_t listenTo) :
    _sessionId(sessionId ? sessionId : _rdGen.rnd<uint64_t>()),
    _protocol(0xAA, 0xBB, 0xCC, 100, *this, 0x2000),
    _commonMessages(_protocol),
    _knownServers(_commonMessages, _rdGen, 15), // max weight TODO from config
    _handshakingPeers(_protocol, _commonMessages, BIND_THIS_MEMFN(on_peer_handshaked), listenTo, _sessionId),
    _connections(_commonMessages, BIND_THIS_MEMFN(connection_removed)),
    _bindToIp(bindTo),
    _port(listenTo)
{
    _protocol.add_message_handler<P2P, PeerState, &P2P::on_ping>(PING_MSG_TYPE, this, 2, 200);
    _protocol.add_message_handler<P2P, PeerState, &P2P::on_pong>(PONG_MSG_TYPE, this, 2, 200);
    _protocol.add_message_handler<P2P, VoidMessage, &P2P::on_known_servers_request>(KNOWN_SERVERS_REQUEST_MSG_TYPE, this, 0, 0);
    _protocol.add_message_handler<P2P, KnownServers, &P2P::on_known_servers>(KNOWN_SERVERS_RESPONSE_MSG_TYPE, this, 0, 2000000);

    _commonMessages.update(KNOWN_SERVERS_REQUEST_MSG_TYPE, VoidMessage());
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
    _timer = set_timer(_rdGen.rnd(1000, 1300), BIND_THIS_MEMFN(on_timer));
    run_async();
}

void P2P::connect_to_servers() {
    static size_t maxActiveConnections = config().get_int("p2p.max_active_connections", 13, 1, 1000);

    size_t nConnections = _connections.total_connected();
    if (nConnections >= maxActiveConnections) return;
    nConnections = maxActiveConnections - nConnections;

    while (nConnections > 0) {
        io::Address a = _knownServers.get_connect_candidate();
        if (a.empty())
            break;

        // TODO check if banned by IP
        // _ipAccess.is_ip_allowed(a.ip());

        LOG_INFO() << "Connecting to " << a << TRACE(_bindToIp)  << TRACE(_sessionId);
        auto result = _reactor->tcp_connect(a, a.u64(), BIND_THIS_MEMFN(on_stream_connected), -1, _bindToIp);
        if (!result) {
            LOG_ERROR() << "Cannot connect to " << a << " error=" << io::error_str(result.error())  << TRACE(_sessionId);
            // TODO schedule reconnect
        }

        --nConnections;
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

    Connection::Ptr conn = std::make_unique<Connection>(
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

    Connection::Ptr conn = std::make_unique<Connection>(
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
    LOG_WARNING() << "Protocol error " << error << " from " << io::Address::from_u64(from) << TRACE(_sessionId);
}

void P2P::on_connection_error(Peer from, io::ErrorCode errorCode) {
    LOG_INFO() << "Connection error from " << io::Address::from_u64(from) << " error=" << io::error_str(errorCode) << TRACE(_sessionId);

    // TODO remove connection
}

void P2P::on_peer_handshaked(Connection::Ptr&& conn, uint16_t listensTo) {
    LOG_INFO() << "Peer handshaked, " << conn->peer_address() << TRACE(listensTo) << TRACE(_sessionId);
    if (listensTo) {
        io::Address newServerAddr = conn->peer_address().port(listensTo);
        if (_knownServers.add_server(newServerAddr, 1)) {
            _peerState.knownServers = _knownServers.get_known_servers().size();
        }
    }
    conn->enable_all_msg_types();
    conn->disable_msg_type(Handshake::RESPONSE_MSG_TYPE);
    conn->disable_msg_type(HandshakeError::MSG_TYPE);
    _connections.add_connection(std::move(conn));
    _peerState.connectedPeers = _connections.total_connected();
    _peerStateDirty = true;
}

void P2P::connection_removed(uint64_t id) {
    LOG_INFO() << "Connection removed, id=" << io::Address::from_u64(id);

    // TODO schedule reconnect
}

void P2P::update_pingpong() {
    if (_peerStateDirty) {
        _commonMessages.update(PING_MSG_TYPE, _peerState);
        _commonMessages.update(PONG_MSG_TYPE, _peerState);
        _peerStateDirty = false;
    }
}

void P2P::on_timer() {
    static int pingPeriod = config().get_int("p2p.ping_period", 2, 1, 600);
    static int queryKnownServersPeriod = config().get_int("p2p.query_known_servers_period", 3, 1, 600);

    if ((_timerCall++ % pingPeriod) == 0) {
        update_pingpong();
        _connections.ping();
    }

    if ((_timerCall++ % queryKnownServersPeriod) == 0) {
        _connections.query_known_servers();
    }
}

bool P2P::on_ping(uint64_t id, PeerState&& state) {
    LOG_DEBUG() << TRACE(_sessionId);
    _connections.update_state(id, std::move(state));
    update_pingpong();
    _connections.pong(id);
    return true;
}

bool P2P::on_pong(uint64_t id, PeerState&& state) {
    LOG_INFO() << TRACE(_sessionId) << " peer " << io::Address::from_u64(id) << " connected to " << state.connectedPeers << " peers";
    _connections.update_state(id, std::move(state));
    return true;
}

bool P2P::on_known_servers_request(uint64_t id, VoidMessage&&) {
    LOG_DEBUG() << TRACE(_sessionId);
    _knownServers.update_known_servers_response();
    _connections.write_msg(id, _commonMessages.get(KNOWN_SERVERS_RESPONSE_MSG_TYPE));
    return true;
}

bool P2P::on_known_servers(uint64_t id, KnownServers&& servers) {
    LOG_DEBUG() << TRACE(_sessionId);
    if (_knownServers.update(servers, false)) {
        _peerState.knownServers = _knownServers.get_known_servers().size();
        _peerStateDirty = true;
        connect_to_servers();
    }
    return true;
}

} //namespace
