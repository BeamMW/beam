#include "p2p/protocol.h"
#include "p2p/connection.h"
#include "utility/logger.h"
#include "utility/bridge.h"
#include "utility/io/tcpserver.h"
#include "utility/io/timer.h"

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
struct NetworkSide : public IMsgHandler, public ILogicToNetwork {
    Protocol<NetworkSide> protocol;
    INetworkToLogic& proxy;
    io::Address address;
    bool thisIsServer;
    io::Reactor::Ptr reactor;
    LogicToNetworkBridge bridge;
    Thread t;
    std::unique_ptr<Connection> connection;

    // just for example, more complex logic can be implied
    PeerLocator someId=0;

    io::TcpServer::Ptr server;
    SerializedMsg msgToSend;

    NetworkSide(INetworkToLogic& _proxy, io::Address _address, bool _thisIsServer) :
        protocol(0xAA, 0xBB, 0xCC, *this, 2000),
        proxy(_proxy),
        address(_address),
        thisIsServer(_thisIsServer),
        reactor(io::Reactor::create(io::Config())),
        bridge(*this, reactor)
    {
        // TODO can be wrapped into macros
        protocol.add_message_handler<Request, &NetworkSide::on_request>(requestCode, 1, 2000000);
        protocol.add_message_handler<Response, &NetworkSide::on_response>(responseCode, 1, 200);
    }

    ILogicToNetwork& get_proxy() {
        return bridge;
    }

    void start() {
        // Reactor objects initialization can be done in any thread...
        // Only event handling matters
        if (thisIsServer) {
            // TODO error handling here
            server = io::TcpServer::create(reactor, address, BIND_THIS_MEMFN(on_stream_accepted));
        } else {
            reactor->tcp_connect(
                address,
                address.packed,
                BIND_THIS_MEMFN(on_client_connected)
            );
        }

        t.start(BIND_THIS_MEMFN(thread_func));
    }

    void thread_func() {
        LOG_DEBUG() << __PRETTY_FUNCTION__ << " starting";
        reactor->run();
        LOG_DEBUG() << __PRETTY_FUNCTION__ << " exiting";
        bridge.stop_rx();
    }

    void stop() {
        reactor->stop();
        t.join();
    }

    void on_stream_accepted(io::TcpStream::Ptr&& newStream, int errorCode) {
        if (errorCode == 0) {
            LOG_DEBUG() << "Stream accepted";
            if (!connection) {
                connection = make_unique<Connection>(
                    protocol,
                    address.packed,
                    100,
                    std::move(newStream)
                );
            }
        } else {
            on_connection_error(address.packed, errorCode);
        }
    }

    void on_client_connected(uint64_t tag, io::TcpStream::Ptr&& newStream, int status) {
        assert(tag == address.packed);
        if (newStream && !connection) {
            connection = make_unique<Connection>(
                protocol,
                address.packed,
                100,
                std::move(newStream)
            );
        } else {
            on_connection_error(tag, status);
        }
    }

    // handles deserialization errors, may optionally notify the logic about that
    void on_protocol_error(uint64_t fromStream, ProtocolError error) override {
        LOG_ERROR() << __FUNCTION__ << "(" << fromStream << "," << error << ")";
    }

    // handles network errors, may optionally notify the logic about that
    void on_connection_error(uint64_t fromStream, int errorCode) override {
        LOG_ERROR() << __FUNCTION__ << "(" << fromStream << "," << errorCode << ")";
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

    bool on_request(uint64_t connectionId, const Request& req) {
        // this assertion is for this test only
        assert(connectionId = address.packed);
        if (!req.is_valid()) return false; // shut down stream

        // TODO const Object& --> Object&&, they are not needed any more on protocol side
        proxy.handle_request(someId, Request(req));
        return true;
    }

    bool on_response(uint64_t connectionId, const Response& res) {
        // this assertion is for this test only
        assert(connectionId = address.packed);
        if (!res.is_valid()) return false; // shut down stream
        proxy.handle_response(someId, Response(res));
        return true;
    }

};

// App-side async thread context
struct AppSideAsyncContext {
    io::Reactor::Ptr reactor;
    NetworkToLogicBridge bridge;
    Thread t;

    AppSideAsyncContext(INetworkToLogic& logicCallbacks) :
        reactor(io::Reactor::create(io::Config())),
        bridge(logicCallbacks, reactor)
    {}

    ~AppSideAsyncContext() {
        t.join();
    }

    INetworkToLogic& get_proxy() {
        return bridge;
    }

    // periodic timer for this example, can also setup a one-shot timer
    io::Timer::Ptr set_timer(unsigned periodMsec, io::Timer::Callback&& onTimer) {
        io::Timer::Ptr timer(io::Timer::create(reactor));
        timer->start(periodMsec, true, std::move(onTimer));
        return timer;
    }

    void run() {
        t.start(BIND_THIS_MEMFN(thread_func));
    }

    void thread_func() {
        LOG_DEBUG() << " starting";
        reactor->run();
        LOG_DEBUG() << " exiting";
        bridge.stop_rx();
    }

    void stop() {
        reactor->stop();
    }

    void wait() {
        t.join();
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
        appLogic(_timerPeriod, _address.packed + (_thisIsServer ? 100500 : 0)),
        networkLogic(appLogic, _address, _thisIsServer)
    {}

    void run() {
        networkLogic.start();
        appLogic.run(&networkLogic.get_proxy());
    }

    void wait() {
        appLogic.wait();
        networkLogic.stop();
    }
};

int main() {
    auto logger = Logger::create(LoggerConfig());
    try {
        constexpr uint16_t port = 32123;

        LOG_INFO() << "Creating apps";

        // server
        App app1(io::Address::localhost().port(port), true, 157);
        // client
        App app2(io::Address::localhost().port(port), false, 183);

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
