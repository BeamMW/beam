#include "../tcpserver.h"
#include "../exception.h"
#include <iostream>

using namespace io;
using namespace std;

void tcpserver_test() {
    try {
        Config config;
        Reactor::Ptr reactor = Reactor::create(config);
        TcpServer::Ptr server = TcpServer::create(
            reactor,
            Address(0, 33333),
            [](TcpStream::Ptr&&, int errorCode) {
                if (errorCode == 0) {
                    cout << "Stream accepted" << endl;
                } else {
                    cout << "Error code " << errorCode << endl;
                }
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


