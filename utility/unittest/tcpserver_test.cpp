#include "utility/io/tcpserver.h"
#include "utility/io/timer.h"
#include "utility/io/exception.h"
#include <iostream>

using namespace beam::io;
using namespace std;

void tcpserver_test() {
    try {
        Config config;
        Reactor::Ptr reactor = Reactor::create(config);
        TcpServer::Ptr server = TcpServer::create(
            reactor,
            Address(0, 33333),
            [&reactor](TcpStream::Ptr&&, int errorCode) {
                if (errorCode == 0) {
                    cout << "Stream accepted" << endl;
                } else {
                    cout << "Error code " << errorCode << endl;
                }
                reactor->stop();
            }
        );

        Timer::Ptr timer = Timer::create(reactor);
        timer->start(
            2000,
            false,
            [&reactor, &timer] {
                // TODO timer->cancel();
                reactor->tcp_connect(Address(Address::LOCALHOST).port(33333), 1, [](uint64_t, shared_ptr<TcpStream>&&, int){});
            }
        );

        cout << "starting reactor..." << endl;
        reactor->run();
        cout << "reactor stopped" << endl;
    }
    catch (const Exception& e) {
        cout << e.what();
    }
}

int main() {
    tcpserver_test();
}


