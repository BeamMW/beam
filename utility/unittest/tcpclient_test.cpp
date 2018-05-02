#include "utility/io/reactor.h"
#include "utility/io/tcpstream.h"
#include "utility/io/exception.h"
#include <iostream>
#include <assert.h>
#include "utility/logger.h"

using namespace beam;
using namespace beam::io;
using namespace std;

Reactor::Ptr reactor;
TcpStream::Ptr theStream;

void on_recv(int what, void* data, size_t size) {
    if (data && size) {
          cout << "RECEIVED:\n" << std::string((const char*)data, size) << "\n";
    } else {
        cout << "ERROR: " << what;
    }
    theStream.reset();
    reactor->stop();
};

void tcpclient_test() {
    try {
        Config config;
        reactor = Reactor::create(config);

        const uint64_t theTag = 202020;

        Reactor::ConnectCallback on_connected = [theTag](uint64_t tag, unique_ptr<TcpStream>&& newStream, int status) {
            assert(tag == theTag);
            if (newStream) {
                theStream = move(newStream);
                theStream->enable_read(on_recv);
                static const char* request = "GET / HTTP/1.0\r\n\r\n";
                theStream->write(request, strlen(request));
            } else {
                cout << "ERROR: " << status;
            }
        };

        Address a;
        // NOTE that this is blocked resolver, TODO add async resolver to Reactor
        a.resolve("example.com");
        a.port(80);

        reactor->tcp_connect(a, theTag, on_connected);

        cout << "starting reactor..." << endl;
        reactor->run();
        cout << "reactor stopped" << endl;
    }
    catch (const Exception& e) {
        cout << e.what();
    }
}

int main() {
    LoggerConfig lc;
    lc.consoleLevel = LOG_LEVEL_VERBOSE;
    lc.flushLevel = LOG_LEVEL_VERBOSE;
    auto logger = Logger::create(lc);
    tcpclient_test();
}



