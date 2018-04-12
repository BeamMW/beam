#include "utility/io/reactor.h"
#include "utility/io/tcpstream.h"
#include "utility/io/exception.h"
#include <iostream>
#include <assert.h>

using namespace beam::io;
using namespace std;

void tcpclient_test() {
    try {
        Config config;
        Reactor::Ptr reactor = Reactor::create(config);

        const uint64_t theTag = 202020;
        TcpStream::Ptr theStream;

        TcpStream::Callback on_recv = [&theStream, &reactor](int what, void* data, size_t size) {
            if (data && size) {
                cout << "RECEIVED:\n" << std::string((const char*)data, size) << "\n";
            } else {
                cout << "ERROR: " << what;
            }
            theStream.reset();
            reactor->stop();
        };

        Reactor::ConnectCallback on_connected = [theTag, &on_recv, &theStream](uint64_t tag, shared_ptr<TcpStream>&& newStream, int status) {
            assert(tag == theTag);
            if (newStream) {
                theStream = move(newStream);
                theStream->enable_read(move(on_recv));
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
    tcpclient_test();
}



