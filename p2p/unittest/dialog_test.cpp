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

#include "p2p/protocol.h"
#include "p2p/connection.h"
#define LOG_DEBUG_ENABLED 0
#include "utility/logger.h"
#include "utility/bridge.h"
#include "utility/io/tcpserver.h"
#include "utility/asynccontext.h"

using namespace beam;
using namespace std;

// peer locators must be (at the moment) convertible to and from uint64_t
// also here can be any id required by logic and convertible into uint64_t
using PeerLocator = uint64_t;

// Serializable request object
struct Request {
    uint64_t x=0;
    std::vector<int> ooo;

    bool operator==(const Request& o) const { return x==o.x && ooo==o.ooo; }

    // network-side validation example
    bool is_valid() const { return x != 0; }

    SERIALIZE(x,ooo);
};

// Response to Request
struct Response {
    uint64_t x=0;
    size_t z=0;

    // network-side validation example
    bool is_valid() const { return x != 0; }

    SERIALIZE(x, z);
};

// Logic->Network requests interface
struct ILogicToNetwork {
    virtual ~ILogicToNetwork() {}
    virtual void send_request(PeerLocator to, Request&& req) = 0;
    virtual void send_response(PeerLocator to, Response&& res) = 0;
};

// Network->Logic callbacks interface
struct INetworkToLogic {
    virtual ~INetworkToLogic() {}
    virtual void handle_request(PeerLocator from, Request&& req) = 0;
    virtual void handle_response(PeerLocator from, Response&& res) = 0;
};

// Bridge Logic->Network
struct LogicToNetworkBridge : public Bridge<ILogicToNetwork> {
    BRIDGE_INIT(LogicToNetworkBridge);
    BRIDGE_FORWARD_IMPL(send_request, Request);
    BRIDGE_FORWARD_IMPL(send_response, Response);
};

// Bridge Network->Logic
struct NetworkToLogicBridge : public Bridge<INetworkToLogic> {
    BRIDGE_INIT(NetworkToLogicBridge);
    BRIDGE_FORWARD_IMPL(handle_request, Request);
    BRIDGE_FORWARD_IMPL(handle_response, Response);
};

enum NetworkMessageCodes : uint8_t {
    // TODO can be wrapped into macros

    requestCode = 3,
    responseCode = 4
};

// Network logic and protocol message handler - can be also unified with NetworkToLogicBridge
struct NetworkSide : public IErrorHandler, public ILogicToNetwork, public AsyncContext {
    Protocol protocol;
    INetworkToLogic& proxy;
    io::Address address;
    bool thisIsServer;
    LogicToNetworkBridge bridge;
    std::unique_ptr<Connection> connection;

    // just for example, more complex logic can be implied
    PeerLocator someId=0;

    io::TcpServer::Ptr server;
    SerializedMsg msgToSend;

    NetworkSide(INetworkToLogic& _proxy, io::Address _address, bool _thisIsServer) :
        protocol(0xAA, 0xBB, 0xCC, 10, *this, 2000),
        proxy(_proxy),
        address(_address),
        thisIsServer(_thisIsServer),
        bridge(*this, *_reactor)
    {
        // TODO can be wrapped into macros
        protocol.add_message_handler<NetworkSide, Request, &NetworkSide::on_request>(requestCode, this, 1, 2000000);
        protocol.add_message_handler<NetworkSide, Response, &NetworkSide::on_response>(responseCode, this, 1, 200);
    }

    ILogicToNetwork& get_proxy() {
        return bridge;
    }

    void start() {
        // Reactor objects initialization can be done in any thread...
        // Only event handling matters
        if (thisIsServer) {
            // TODO error handling here
            server = io::TcpServer::create(*_reactor, address, BIND_THIS_MEMFN(on_stream_accepted));
        } else {
            _reactor->tcp_connect(
                address,
                address.u64(),
                BIND_THIS_MEMFN(on_client_connected)
            );
        }

        run_async([]{}, [this]{ bridge.stop_rx(); });
    }

    void stop_and_wait() {
        stop();
        wait();
    }

    void on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode) {
        if (errorCode == 0) {
            LOG_DEBUG() << "Stream accepted";
            if (!connection) {
                connection = make_unique<Connection>(
                    protocol,
                    address.u64(),
                    Connection::inbound,
                    100,
                    std::move(newStream)
                );
                //connection->disable_all_msg_types();
            }
        } else {
            on_connection_error(address.u64(), errorCode);
        }
    }

    void on_client_connected(uint64_t tag, io::TcpStream::Ptr&& newStream, io::ErrorCode status) {
        assert(tag == address.u64());
        if (newStream && !connection) {
            connection = make_unique<Connection>(
                protocol,
                address.u64(),
                Connection::outbound,
                100,
                std::move(newStream)
            );
            //connection->disable_all_msg_types();
            //connection->disable_msg_type(4);
        } else {
            on_connection_error(tag, status);
        }
    }

    // handles deserialization errors, may optionally notify the logic about that
    void on_protocol_error(uint64_t fromStream, ProtocolError error) override {
        LOG_ERROR() << __FUNCTION__ << "(" << fromStream << "," << error << ")";
    }

    // handles network errors, may optionally notify the logic about that
    void on_connection_error(uint64_t fromStream, io::ErrorCode errorCode) override {
        LOG_ERROR() << __FUNCTION__ << "(" << fromStream << "," << io::error_str(errorCode) << ")";
    }

    void on_unexpected_msg(uint64_t fromStream, MsgType type) override {
        LOG_ERROR() << __FUNCTION__ << "(" << fromStream << "," << unsigned(type) << ")";
    }

    void send_request(PeerLocator to, Request&& req) override {
        someId = to;
        if (connection) {
            protocol.serialize(msgToSend, requestCode, req);

            connection->write_msg(msgToSend); // TODO handle dead connection

            // not needed any more (in this test)
            msgToSend.clear();
        } else {
            LOG_ERROR() << "No connection";
            // add some handling
        }
    }

    void send_response(PeerLocator to, Response&& res) override {
        someId = to;
        if (connection) {
            protocol.serialize(msgToSend, responseCode, res);
            connection->write_msg(msgToSend);

            // not needed any more (in this test)
            msgToSend.clear();
        } else {
            LOG_ERROR() << "No connection";
            // add some handling
        }
    }

    bool on_request(uint64_t connectionId, Request&& req) {
        // this assertion is for this test only
        assert(connectionId == address.u64());
        if (!req.is_valid()) return false; // shut down stream

        proxy.handle_request(someId, std::move(req));
        return true;
    }

    bool on_response(uint64_t connectionId, Response&& res) {
        // this assertion is for this test only
        assert(connectionId == address.u64());
        if (!res.is_valid()) return false; // shut down stream
        proxy.handle_response(someId, std::move(res));
        return true;
    }

};

// App-side async thread context
struct AppSideAsyncContext : public AsyncContext {
    NetworkToLogicBridge bridge;

    AppSideAsyncContext(INetworkToLogic& logicCallbacks) :
        bridge(logicCallbacks, *_reactor)
    {}

    INetworkToLogic& get_proxy() {
        return bridge;
    }

    void run() {
        run_async([]{}, [this]{ bridge.stop_rx(); });
    }
};

// Application logic, resides in thread distinct to network one
struct EventDrivenAppLogic : INetworkToLogic {
    AppSideAsyncContext ctx;
    unsigned timerPeriod=1000;
    io::Timer::Ptr timer;
    PeerLocator someId = 0;
    int counter = 0;
    ILogicToNetwork* proxy=0;

    EventDrivenAppLogic(unsigned _timerPeriod, PeerLocator id) :
        ctx(*this), timerPeriod(_timerPeriod), someId(id)
    {}

    void run(ILogicToNetwork* _proxy) {
        proxy = _proxy;
        timer = ctx.set_timer(timerPeriod, BIND_THIS_MEMFN(on_timer));
        ctx.run();
    }

    void stop() {
        ctx.stop();
    }

    void wait() {
        ctx.wait();
    }

    void on_timer() {
        if (counter > 10) {
            LOG_INFO() << "stopping";
            stop();
            return;
        }
        LOG_DEBUG() << "sending request, x=" << ++counter;
        Request req;
        req.x = counter;
        for (int i=0, sz = 13 * counter; i<sz; ++i) req.ooo.push_back(i);
        if (proxy) proxy->send_request(someId, std::move(req));
    }

    void handle_request(PeerLocator from, Request&& req) override {
        LOG_INFO() << "Request from " << from << " x=" << req.x << " size=" << req.ooo.size();
        Response res { req.x, req.ooo.size() };
        if (proxy) proxy->send_response(from, std::move(res));
    }

    void handle_response(PeerLocator from, Response&& res) override {
        LOG_INFO() << "Response from " << from << " x=" << res.x << " z=" << res.z;
    }
};

struct App {
    EventDrivenAppLogic appLogic;
    NetworkSide networkLogic;

    App(io::Address _address, bool _thisIsServer, unsigned _timerPeriod) :
        appLogic(_timerPeriod, _thisIsServer ? 1 : 2),
        networkLogic(appLogic, _address, _thisIsServer)
    {}

    void run() {
        networkLogic.start();
        appLogic.run(&networkLogic.get_proxy());
    }

    void wait() {
        appLogic.wait();
        networkLogic.stop_and_wait();
    }
};

int main() {
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = Logger::create(logLevel, logLevel);
    try {
        constexpr uint16_t port = 32123;

        LOG_DEBUG() << "Creating apps";

        // server
        App app1(io::Address::localhost().port(port), true, 57);
        // client
        App app2(io::Address::localhost().port(port), false, 83);

        LOG_INFO() << "Starting apps";

        app1.run();
        app2.run();

        LOG_INFO() << "Waiting";

        app1.wait();
        app2.wait();

        LOG_INFO() << "Done";
    } catch (const std::exception& e) {
        LOG_ERROR() << "Exception: " << e.what();
    } catch (...) {
        LOG_ERROR() << "Unknown exception";
    }
}
