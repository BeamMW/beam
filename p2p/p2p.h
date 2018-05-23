#pragma once
#include "servers.h"
#include "banned_peers.h"
#include "protocol.h"
#include "utility/asynccontext.h"
#include "utility/io/tcpserver.h"

namespace beam {

class P2P : public IErrorHandler, protected AsyncContext {
public:
    P2P(io::Address bindTo, uint16_t listenTo);
    ~P2P();

    void add_server(io::Address a);

    void start();

private:
    // IMsgHandler impl
    void on_protocol_error(Peer from, ProtocolError error) override {}
    void on_connection_error(Peer from, int errorCode) override {}

    void on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode) {
        // is ip banned ? if yes, close

        // TODO nonce is stream ID

        // wait for handshake request


    }
    void on_stream_connected(Peer peer, io::TcpStream::Ptr&& newStream, int status) {
        // send handshake

        // wait for handshake response
    }

    void connect_to_servers();

    Protocol _protocol;
    io::Address _ip;
    uint16_t _port; // !=0 if this is server
    io::TcpServer::Ptr _server;
    SerializedMsg _msgToSend;
};

} //namespace
