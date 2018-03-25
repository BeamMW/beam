#include "../tcpserver.h"
#include "../exception.h"
#include <iostream>

using namespace io;
using namespace std;

// TODO use catch2 TF
// TODO use spdlog

void tcpserver_test() {
    try {
        Reactor::Ptr reactor = Reactor::create();
        TcpServer server(
            reactor,
            33333,
            [](TcpStream&&, int errorCode) {
                if (errorCode == 0) {
                    cout << "Stream accepted" << endl;
                } else {
                    cout << "Error code " << errorCode << endl;
                }
            },
            true
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


