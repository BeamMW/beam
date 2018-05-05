#include "utility/io/tcpserver.h"
#include "utility/io/timer.h"
#include "utility/logger.h"
#include <iostream>
#include <assert.h>

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
                    cout << "Stream accepted" << endl;
                    assert(newStream);
                } else {
                    cout << "Error code " << errorCode << endl;
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

        cout << "starting reactor..." << endl;
        reactor->run();
        cout << "reactor stopped" << endl;
    }
    catch (const std::exception& e) {
        cout << e.what();
    }
}

int main() {
    LoggerConfig lc;
    lc.consoleLevel = LOG_LEVEL_VERBOSE;
    lc.flushLevel = LOG_LEVEL_VERBOSE;
    auto logger = Logger::create(lc);
    tcpserver_test();
}


