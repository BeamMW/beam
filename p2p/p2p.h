#pragma once
#include "connection.h"
#include "utility/message_queue.h"

namespace beam {

class P2P {
public:
    /// Will be more complex scheme soon...
    P2P(io::Address listenAddress, const std::vector<io::Address>& _connectTo);

    ~P2P();

    TX<Message::Ptr> run() {
        io::Config config;
        _reactor = io::Reactor::create(config);

        for (auto addr: _connectTo) {
            // connect to peers
            bool result = _reactor->tcp_connect(
                addr, ++_connIdCounter,
                [this](uint64_t connId, TcpStream::Ptr&& newStream, int status) {
                    if (newStream) {
                        on_new_connection_active(connId, std::move(newStream), status);
                    } else {
                        on_connect_error(status);
                    }
                }
            );

            // error handling
            if (!result) {
                on_connect_error(_reactor->get_last_error());
            }
        }

        //listen
        if (_listenTo) {
            _server = TcpServer::create(
                _reactor,
                _listenTo,
                [this](TcpStream::Ptr&& newStream, int errorCode) {
                    if (errorCode == 0) {
                        on_server_error(errorCode);
                    } else {
                        on_new_connection_passive(std::move(newStream));
                    }
                }
            );
        }

        /*auto f = */_reactor->run();

        return _rx.get_tx();
    }

    // callbacks from connection
    void data_received(uint64_t connId, int status, protocol::MsgType type, const void* rawData, size_t size);

    // callbacks from bridge
    bool send(const PingStub& ping);
    bool send(const PongStub& pong);
    bool send(const TransactionStub& tx);

private:
    // NodeToNetwork impl
    void send_transaction(const TransactionStub& tx) override;

    // callback from peers
    void on_message(uint64_t fromId, Message::Ptr&& msg);

    void on_server_error(int errorCode);

    void on_connect_error(int errorCode);

    // new connection from listening socket
    void on_new_connection_passive(TcpStream::Ptr&& stream);

    // new active connection
    void on_new_connection_active(uint64_t connId, TcpStream::Ptr&& newStream, int status);


    io::Address _listenTo;
    std::vector<io::Address> _connectTo; // this is stub
    std::unordered_map<uint64_t, Connection> _connections;
    uint64_t _connIdCounter=0;
    io::Reactor::Ptr _reactor;
    RX<Message::Ptr> _rx;
    TcpServer::Ptr _server;
    std::future<void> _future;
};
} //namespace
