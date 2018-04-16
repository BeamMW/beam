#include "p2p/connection.h"
#include "p2p/msg_serializer.h"
#include "p2p/protocol.h"
#include "utility/io/tcpserver.h"
#include "utility/io/timer.h"
#include <iostream>
#include <thread>
#include <unistd.h>

using ThreadPtr = std::unique_ptr<std::thread>;

using namespace beam;
using namespace beam::io;
using namespace std;

struct Thread {
    ThreadPtr theThread;

    void start() {
        theThread = make_unique<thread>([this]() { thread_func(); });
    }

    void join() {
        if (theThread) {
            theThread->join();
            theThread.reset();
        }
    }

    virtual ~Thread() {
        assert(!theThread);
    }

    virtual void thread_func() = 0;
};

constexpr uint16_t g_port = 33333;

constexpr MsgType msgTypeForSomeObject = 222;

// Test object being transferred
struct SomeObject {
    int i=0;
    size_t x=0;
    std::vector<int> ooo;

    bool operator==(const SomeObject& o) const { return i==o.i && x==o.x && ooo==o.ooo; }

    SERIALIZE(i,x,ooo);
};

// Message handler for both sides in this test
struct MessageHandler : IMsgHandler {
    void on_protocol_error(uint64_t fromStream, ProtocolError error) override {
        cout << __FUNCTION__ << "(" << fromStream << "," << error << ")" << endl;
    }

    void on_connection_error(uint64_t fromStream, int errorCode) override {
        cout << __FUNCTION__ << "(" << fromStream << "," << errorCode << ")" << endl;
    }

    bool on_some_object(uint64_t fromStream, const SomeObject& msg) {
        cout << __FUNCTION__ << "(" << fromStream << "," << msg.i << ")" << endl;
        receivedObj = msg;
        return true;
    }

    SomeObject receivedObj;
};

struct Server : Thread {
    MessageHandler handler;
    Protocol<MessageHandler> protocol;
    unique_ptr<Connection> connection;

    Server() :
        protocol(0xAA, 0xBB, 0xCC, handler)
    {
        protocol.add_message_handler<SomeObject, &MessageHandler::on_some_object>(msgTypeForSomeObject, 8, 10000000);
    }

    void thread_func() {
        cout << "In server thread" << endl;

        try {
            Config config;
            Reactor::Ptr reactor = Reactor::create(config);
            TcpServer::Ptr server = TcpServer::create(
                reactor,
                Address::localhost().port(g_port),
                [this](TcpStream::Ptr&& newStream, int errorCode) {
                    if (errorCode == 0) {
                        cout << "Stream accepted" << endl;
                        on_stream_accepted(move(newStream));
                    } else {
                        cout << "Error code " << errorCode << endl;
                    }
                }
            );

            Timer::Ptr timer = Timer::create(reactor);
            timer->start(
                1000,
                false,
                [&reactor] {
                    reactor->stop();
                }
            );

            cout << "starting reactor..." << endl;
            reactor->run();
            cout << "reactor stopped" << endl;
        }
        catch (const std::exception& e) {
            cout << e.what();
        }
    }

    void on_stream_accepted(TcpStream::Ptr&& newStream) {
        if (!connection) {
            connection = make_unique<Connection>(
                protocol,
                12345,
                100,
                move(newStream)
            );
        }
    }
};

struct Client : Thread {
    MessageHandler handler;
    Protocol<MessageHandler> protocol;
    unique_ptr<Connection> connection;

    MsgSerializer serializer;
    vector<SharedBuffer> serializedMsg;

    Reactor::Ptr reactor;

    static constexpr uint64_t streamId = 13;

    SomeObject msg;

    Client() :
        protocol(0xAA, 0xBB, 0xCC, handler),
        serializer(200, protocol.get_default_header())
    {
        protocol.add_message_handler<SomeObject, &MessageHandler::on_some_object>(msgTypeForSomeObject, 8, 10000000);
    }

    void thread_func() {
        cout << "In client thread" << endl;

        try {

            Config config;
            reactor = Reactor::create(config);

            reactor->tcp_connect
                (Address::localhost().port(g_port),
                 streamId,
                [this](uint64_t tag, TcpStream::Ptr&& newStream, int status) { on_client_connected(tag, move(newStream), status); }
            );

            Timer::Ptr timer = Timer::create(reactor);
            timer->start(
                1000,
                false,
                [this] {
                    reactor->stop();
                }
            );

            cout << "starting reactor..." << endl;
            reactor->run();
            cout << "reactor stopped" << endl;

        }
        catch (const std::exception& e) {
            cout << e.what() << endl;
        }
        catch (...) {
            cout << "Unknown exception" << endl;
        }

    }

    void produce_message() {
        serializer.new_message(msgTypeForSomeObject);

        msg.i = 3;
        msg.x = 0xFFFFFFFF;
        msg.ooo.reserve(1000000);
        for (int i=0; i<1000000; ++i) msg.ooo.push_back(i);

        serializer & msg;

        serializer.finalize(serializedMsg);
    }

    void on_client_connected(uint64_t tag, TcpStream::Ptr&& newStream, int status) {
        assert(tag == streamId);
        if (newStream && !connection) {
            connection = make_unique<Connection>(
                protocol,
                streamId,
                100,
                move(newStream)
            );

            produce_message();

            connection->write_msg(serializedMsg);
        } else {
            handler.on_connection_error(streamId, status);
        }
    }
};

void twopeers_test() {
    Server server;
    server.start();

    usleep(500);

    Client client;
    client.start();

    client.join();
    server.join();

    assert(client.msg == server.handler.receivedObj);
}

int main() {
    try {
        twopeers_test();
    } catch (const std::exception& e) {
        cout << "Exception: " << e.what() << "\n";
    }
}
