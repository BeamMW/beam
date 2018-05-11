#include "utility/io/tcpserver.h"
#include "utility/io/timer.h"
#include <assert.h>

#define LOG_VERBOSE_ENABLED 0
#include "utility/logger.h"

using namespace beam;
using namespace beam::io;
using namespace std;

Reactor::Ptr reactor;
Timer::Ptr timer;

void on_timer() {
    timer->cancel();
    reactor->tcp_connect(Address(Address::LOCALHOST).port(33333), 1, [](uint64_t, shared_ptr<TcpStream>&&, int){});
}

void tcpserver_test() {
    try {
        reactor = Reactor::create();
        TcpServer::Ptr server = TcpServer::create(
            reactor,
            Address(0, 33333),
            [](TcpStream::Ptr&& newStream, int errorCode) {
                if (errorCode == 0) {
                    LOG_DEBUG() << "Stream accepted, socket=" << newStream->address().str() << " peer=" << newStream->peer_address().str();
                    assert(newStream);
                } else {
                    LOG_ERROR() << "Error code=" << errorCode;
                }
                reactor->stop();
            }
        );

        timer = Timer::create(reactor);
        timer->start(
            200,
            true,//false,
            on_timer
        );

        LOG_DEBUG() << "starting reactor...";
        reactor->run();
        LOG_DEBUG() << "reactor stopped";
    }
    catch (const std::exception& e) {
        LOG_ERROR() << e.what();
    }
}

int main() {
    LoggerConfig lc;
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    lc.consoleLevel = logLevel;
    lc.flushLevel = logLevel;
    auto logger = Logger::create(lc);
    tcpserver_test();
}


