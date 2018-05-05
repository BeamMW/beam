#include "utility/io/address.h"
#include <iostream>
#include <assert.h>

using namespace beam::io;
using namespace std;

void address_test() {
    Address a;
    // getaddrinfo leaks memory
    for (int i=0; i<100; ++i) a.resolve("google.com");
    Address b;
    b.resolve("google.com:666");
    cout << a.str() << " " << b.str() << endl;
    assert(a.port() == 0 && b.port() == 666 && a.ip() != 0 && b.ip() != 0);
    b.resolve("localhost");
    cout << b.str() << endl;
    assert(b == Address::LOCALHOST);
}

#ifdef WIN32
struct WSAInit {
    WSAInit() {
        WSADATA wsaData = { 0 };
        int errorno = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (errorno != 0) {
            throw std::runtime_error("Failed to init WSA");
        }
    }
    ~WSAInit() {
        WSACleanup();
    }
};
#endif

int main() {
#ifdef WIN32
    WSAInit init;
#endif // !WIN32
    address_test();
}

