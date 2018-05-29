#pragma once
#include "rnd_gen.h"
#include "servers.h"
#include "handshake.h"
#include "banned_peers.h"
#include "protocol.h"
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
    void on_connection_error(Peer from, int errorCode) override;

    void on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode);

    void on_stream_connected(Peer peer, io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode);


    void connect_to_servers();

    void on_peer_handshaked(ConnectionPtr&& conn, uint16_t listensTo);

    RandomGen _rdGen;
    Servers _knownServers;
    Protocol _protocol;
    HandshakingPeers _handshakingPeers;
    io::Address _bindToIp;
    uint16_t _port; // !=0 if this is server
    io::TcpServer::Ptr _thisServer;
    SerializedMsg _msgToSend;
};

} //namespace
