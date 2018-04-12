#include "p2p/protocol.h"
#include "utility/io/reactor.h"
#include "utility/io/tcpserver.h"
#include "utility/io/exception.h"
#include "utility/message_queue.h"
#include <iostream>
#include <list>
#include <future>
#include <assert.h>

using namespace io;
using namespace std;

namespace beam {

// TODO proof of concepts here

struct PingStub {
    uint64_t data;
};

struct PongStub {
    uint64_t data;
};

struct TransactionStub {
    uint64_t data;
    std::list<uint64_t> moreData;
};

class NetworkToNode {
public:
    virtual ~NetworkToNode() {}
    virtual void on_transaction(const TransactionStub& tx) = 0;
};

class NodeToNetwork {
public:
    virtual ~NodeToNetwork() {}
    virtual void send_transaction(const TransactionStub& tx) = 0;
};

struct Message {
    using Ptr = std::unique_ptr<Message>;

    protocol::MsgType type;

    virtual ~Message() {}

    virtual void transfer(NetworkToNode& bridge) = 0;

    explicit Message(protocol::MsgType t) : type(t) {}
};

struct PingMessage : Message, PingStub {
    void transfer(NetworkToNode&) {}
};

struct PongMessage : Message, PongStub {
    void transfer(NetworkToNode&) {}
};

struct TxMessage : Message, TransactionStub {
    void transfer(NetworkToNode& bridge) {
        bridge.on_transaction(*this);
    }
};

//~etc

class Connection {
public:
    using Callback = std::function<void(uint64_t connId, int status, protocol::MsgType type, const void* rawData, size_t size)>;

    Connection(uint64_t id, TcpStream::Ptr&& stream, const Callback& callback) :
        _id(id),
        _stream(std::move(stream)),
        _callback(callback),
        _state(reading_header),
        _unread(protocol::MsgHeader::SIZE)
    {
        assert(stream);
        assert(callback);

        _stream->enable_read([this](int what, void* data, size_t size){ on_recv(what, data, size); });
    }

    ~Connection() {}


private:
    enum State { reading_header, reading_message, disconnected };

    void on_recv(int what, void* data, size_t size) {
        if (what != 0) {
            // disconnected
            _state = disconnected;
            _callback(_id, what, protocol::MsgType::null, 0, 0);
        } else if (_state == reading_header) {
            read_header(data, size);
        } else if (_state == reading_message) {
            read_message(data, size);
        }
    }

    void read_header(const void* data, size_t size);
    void read_message(const void* data, size_t size);

    uint64_t _id;
    TcpStream::Ptr _stream;
    Callback _callback;
    State _state;
    std::vector<uint8_t> _buffer; //serialized read buffer
    size_t _unread; // data remaining to deserialize
    protocol::MsgHeader _header;
};

class Peers : public NodeToNetwork {
public:
    Peers(Address listenAddress, const std::vector<Address>& _peers);

    ~Peers();

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


    Address _listenTo;
    std::vector<Address> _connectTo; // this is stub
    std::unordered_map<uint64_t, Connection> _connections;
    uint64_t _connIdCounter=0;
    io::Reactor::Ptr _reactor;
    RX<Message::Ptr> _rx;
    TcpServer::Ptr _server;
    std::future<void> _future;
};

struct WalletStub : public NetworkToNode {
private:
    // NetworkToNode impl
    void on_transaction(const TransactionStub& tx) override;

    io::Reactor::Ptr _reactor;
};

class NodeStub {
    Peers peers;
    WalletStub wallet;
};

} //namespace

void connectivity_stub_test() {
    try {

    }
    catch (const Exception& e) {
        cout << e.what();
    }
}

int main() {
    connectivity_stub_test();
}




