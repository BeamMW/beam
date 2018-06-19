#pragma once
#include "types.h"
#include "p2p_settings.h"
#include "rnd_gen.h"
#include "handshake.h"
#include "connected_peers.h"
#include "protocol.h"

#include "utility/asynccontext.h"
#include "utility/io/tcpserver.h"

namespace beam {

class P2P : public IErrorHandler, protected AsyncContext {
public:
    explicit P2P(P2PSettings settings);
    ~P2P();

    void start();

private:
    // IErrorHandler overrides
    void on_protocol_error(uint64_t id, ProtocolError error) override;
    void on_connection_error(uint64_t id, io::ErrorCode errorCode) override;

    void on_start_server(TimerID);
    void on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode);

    void on_connect_to_peers(TimerID);
    void on_stream_connected(uint64_t id, io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode);

    // Handshakes callbacks
    void on_peer_handshaked(Connection::Ptr&& conn, bool isServer);
    void on_handshake_error(StreamId streamId, const HandshakeError& e);

    void on_peer_removed(StreamId streamId);

    void cleanup_connection(StreamId streamId);

    RandomGen           _rdGen;
    P2PSettings         _settings;
    io::TcpServer::Ptr  _thisServer;
    ConnectPool         _connectPool;
    Protocol            _protocol;
    HandshakingPeers    _handshakes;
    ConnectedPeers      _connectedPeers;

    KnownServers        _knownServers;
    bool                _knownServersUpdated=false;
    io::SharedBuffer    _knownServersMsg;

    PeerState           _peerState;
    bool                _peerStateUpdated=false;
    io::SharedBuffer    _pingMsg;
    io::SharedBuffer    _pongMsg;
};

/*

class P2P : public IErrorHandler, protected AsyncContext {
public:
    P2P(uint64_t sessionId, io::Address bindTo, uint16_t listenTo);
    ~P2P();

    void add_known_servers(const KnownServers& servers);

    void start();

private:
    // IMsgHandler impl
    void on_protocol_error(uint64_t from, ProtocolError error) override;
    void on_connection_error(uint64_t from, io::ErrorCode errorCode) override;

    void on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode);

    void on_stream_connected(uint64_t peer, io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode);

    void connect_to_servers();

    void on_peer_handshaked(Connection::Ptr&& conn, uint16_t listensTo);

    void connection_removed(uint64_t id);

    void on_timer();

    void update_pingpong();

    bool on_ping(uint64_t id, PeerState&& state);
    bool on_pong(uint64_t id, PeerState&& state);
    bool on_known_servers_request(uint64_t id, VoidMessage&&);
    bool on_known_servers(uint64_t id, KnownServers&& servers);

    PeerState _peerState;
    bool _peerStateDirty=true;

    RandomGen _rdGen;
    uint64_t _sessionId;
    Protocol _protocol;
    CommonMessages _commonMessages;
    Servers _knownServers;
    HandshakingPeers _handshakingPeers;
    Connections _connections;
    //IpAccessControl _ipAccess;
    io::Address _bindToIp;
    uint16_t _port; // !=0 if this is server
    io::TcpServer::Ptr _thisServer;
    SerializedMsg _msgToSend;
    io::Timer::Ptr _timer;
    int _timerCall=0;
};

*/

} //namespace
