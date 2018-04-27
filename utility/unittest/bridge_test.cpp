#include "utility/bridge.h"
#include "utility/logger.h"
#include <assert.h>

#ifdef WIN32
#define __PRETTY_FUNCTION__ __FUNCSIG__
#endif // WIN32


using namespace std;
using namespace beam;

// Some requests to be forwarded to another thread
struct Req1 { int x=0; };
struct Req2 { std::string x; };
struct StopRequest {};

// Some interface
struct IXxx {
    virtual ~IXxx() {}
    virtual void send_req_1(uint64_t to, Req1&& r)=0;
    virtual void send_req_2(uint64_t to, Req2&& r)=0;
    virtual void stop(uint64_t to, StopRequest&& r)=0;
};

// Inter-thread bridge impl
struct XxxBridge : public Bridge<IXxx> {
    /*
    using Base = Bridge<IXxx>;
    XxxBridge(IXxx& _forwardTo, const io::Reactor::Ptr& _reactor) :
        Base(_forwardTo, _reactor)
    {}
    */
    BRIDGE_INIT(XxxBridge);

    BRIDGE_FORWARD_IMPL(send_req_1, Req1);
    BRIDGE_FORWARD_IMPL(send_req_2, Req2);
    BRIDGE_FORWARD_IMPL(stop, StopRequest);
};

// Implementation of IXxx that executes in remote thread
struct Xxx : IXxx {
    io::Reactor::Ptr reactor;

    Xxx(const io::Reactor::Ptr& _reactor) : reactor(_reactor) {}

    virtual void send_req_1(uint64_t to, Req1&& r) {
        LOG_DEBUG() << __PRETTY_FUNCTION__ << " " << to << " " << r.x;
    }
    virtual void send_req_2(uint64_t to, Req2&& r) {
        LOG_DEBUG() << __PRETTY_FUNCTION__ << " " << to << " " << r.x;
    }
    virtual void stop(uint64_t to, StopRequest&&) {
        LOG_DEBUG() << __PRETTY_FUNCTION__;

        // This is for illustration, reactor can be stopped from any thread
        reactor->stop();
    }
};

// Some logic that executes in a remote thread
struct RemoteThreadLogicExample {
    io::Reactor::Ptr reactor;
    Xxx handler;
    XxxBridge bridge;
    Thread t;

    RemoteThreadLogicExample() :
        reactor(io::Reactor::create(io::Config())),
        handler(reactor),
        bridge(handler, reactor)
    {}

    void start() {
        t.start(BIND_THIS_MEMFN(thread_func));
    }

    void thread_func() {
        LOG_DEBUG() << __PRETTY_FUNCTION__ << " starting";
        reactor->run();
        LOG_DEBUG() << __PRETTY_FUNCTION__ << " exiting";
        bridge.stop_rx();
    }

    void stop() {
        // In this example, it's stopped from the queue
        // reactor->stop();
        t.join();
    }
};

void send_inter_thread(IXxx& xxx) {
    xxx.send_req_1(33, Req1{ 202020 });
    xxx.send_req_2(66, Req2{ "zzzzzzzzzzzzzzzzzzzzzzzzz" });
    xxx.send_req_1(99, Req1{ 543210 });
    xxx.stop(0, StopRequest{});
}

int main() {
    auto logger = Logger::create(LoggerConfig());

    RemoteThreadLogicExample remoteLogic;
    IXxx& proxyXxx = remoteLogic.bridge;

    remoteLogic.start();

    send_inter_thread(proxyXxx);

    remoteLogic.stop();
}


