#pragma once
#include "rnd_gen.h"
#include "servers.h"
#include "handshake.h"
#include "ip_access_control.h"
#include "common_messages.h"
#include "connections.h"
#include "utility/asynccontext.h"
#include "utility/io/tcpserver.h"

#include "utility/logger.h"

namespace beam {

class P2P : public IErrorHandler, protected AsyncContext {
public:
    P2P(io::Address bindTo, uint16_t listenTo);
    ~P2P();

    void add_known_servers(const KnownServers& servers);

    void start();

private:
    // IMsgHandler impl
    void on_protocol_error(Peer from, ProtocolError error) override;
    void on_connection_error(Peer from, io::ErrorCode errorCode) override;

    void on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode);

    void on_stream_connected(Peer peer, io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode);

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
    Servers _knownServers;
    Protocol _protocol;
    CommonMessages _commonMessages;
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

} //namespace
