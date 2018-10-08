// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "p2p/connection.h"
#include "p2p/msg_serializer.h"
#include "p2p/protocol.h"
#include "utility/io/tcpserver.h"
#include "utility/io/timer.h"
#include "utility/helpers.h"
#include <iostream>
//#include <unistd.h>

using namespace beam;
using namespace beam::io;
using namespace std;

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

// Response to SomeObject
struct Response {
    size_t z=0;

    SERIALIZE(z);
};

// Message handler for both sides in this test
// It resides on network side and handles local logic
struct MessageHandler : IErrorHandler {

    void on_protocol_error(uint64_t fromStream, ProtocolError error) override {
        cout << __FUNCTION__ << "(" << fromStream << "," << error << ")" << endl;
		Reactor::get_Current().stop();
    }

    void on_connection_error(uint64_t fromStream, io::ErrorCode errorCode) override {
        cout << __FUNCTION__ << "(" << fromStream << "," << errorCode << ")" << endl;
		Reactor::get_Current().stop();
	}

    bool on_some_object(uint64_t fromStream, SomeObject&& msg) {
        cout << __FUNCTION__ << "(" << fromStream << "," << msg.i << ")" << endl;
        receivedObj = msg;
		Reactor::get_Current().stop();
		return true;
    }

    bool on_response(uint64_t fromStream, Response&& msg) {
        cout << __FUNCTION__ << "(" << fromStream << "," << msg.z << ")" << endl;
        receivedResponse = msg;
        return true;
    }

    SomeObject receivedObj;
    Response receivedResponse;
};

struct Server {
    MessageHandler handler;
    Protocol protocol;
    unique_ptr<Connection> connection;
    Thread t;

    Server() :
        protocol(0xAA, 0xBB, 0xCC, 256, handler, 200)
    {
        protocol.add_message_handler<MessageHandler, SomeObject, &MessageHandler::on_some_object>(msgTypeForSomeObject, &handler,8, 10000000);
    }

    void start() {
        t.start(BIND_THIS_MEMFN(thread_func));
    }

    void join() {
        t.join();
    }

    void thread_func() {
        cout << "In server thread" << endl;

        try {
            Reactor::Ptr reactor = Reactor::create();
			Reactor::Scope scope(*reactor);

            TcpServer::Ptr server = TcpServer::create(
                *reactor, Address::localhost().port(g_port), BIND_THIS_MEMFN(on_stream_accepted)
            );

            Timer::Ptr timer = Timer::create(*reactor);
            timer->start(
                10000,
                false,
                [] {
                    Reactor::get_Current().stop();
                }
            );

            cout << "starting reactor..." << endl;
            reactor->run();
            cout << "reactor stopped" << endl;

			connection.reset();
        }
        catch (const std::exception& e) {
            cout << e.what();
        }
    }

    void on_stream_accepted(TcpStream::Ptr&& newStream, int errorCode) {
        if (errorCode == 0) {
            cout << "Stream accepted" << endl;
            if (!connection) {
                connection = make_unique<Connection>(
                    protocol,
                    12345,
                    Connection::inbound,
                    100,
                    move(newStream)
                );
            }
        } else {
            cout << "Error code " << errorCode << endl;
        }

    }
};

struct Client {
    MessageHandler handler;
    Protocol protocol;
    unique_ptr<Connection> connection;

    vector<SharedBuffer> serializedMsg;

    static constexpr uint64_t streamId = 13;

    SomeObject msg;

    Thread t;

    Client() :
        protocol(0xAA, 0xBB, 0xCC, 256, handler,200)
    {
        protocol.add_message_handler<MessageHandler, SomeObject, &MessageHandler::on_some_object>(msgTypeForSomeObject, &handler, 8, 10000000);
    }

    void start() {
        t.start(BIND_THIS_MEMFN(thread_func));
    }

    void join() {
        t.join();
    }

    void thread_func() {
        cout << "In client thread" << endl;

        try {
			Reactor::Ptr reactor = Reactor::create();
			Reactor::Scope scope(*reactor);

            reactor->tcp_connect
                (Address::localhost().port(g_port),
                streamId,
                BIND_THIS_MEMFN(on_client_connected)
            );

            Timer::Ptr timer = Timer::create(*reactor);
            timer->start(
                10000,
                false,
                [] {
                    Reactor::get_Current().stop();
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
        msg.i = 3;
        msg.x = 0xFFFFFFFF;
        msg.ooo.reserve(1000000);
        for (int i=0; i<1000000; ++i) msg.ooo.push_back(i);
        protocol.serialize(serializedMsg, msgTypeForSomeObject, msg);
    }

    void on_client_connected(uint64_t tag, TcpStream::Ptr&& newStream, io::ErrorCode status) {
        assert(tag == streamId);
        if (newStream && !connection) {
            connection = make_unique<Connection>(
                protocol,
                streamId,
                Connection::outbound,
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

    this_thread::sleep_for(chrono::microseconds(500));
    //usleep(500);

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
