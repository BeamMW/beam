// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "utility/io/address.h"
#include <iostream>
#include <assert.h>

using namespace beam::io;
using namespace std;

void address_test() {
    Address a;
    // getaddrinfo leaks memory
    for (int i=0; i<100; ++i) a.resolve("beam-mw.com");
    Address b;
    b.resolve("beam-mw.com:666");
    cout << a.str() << " " << b.str() << endl;
    assert(a.port() == 0 && b.port() == 666 && a.ip() != 0 && b.ip() != 0);
    Address c;
    c.resolve("localhost");
    cout << c.str() << endl;
    assert(c == Address::LOCALHOST);
    sockaddr_in sa;
    b.fill_sockaddr_in(sa);
    Address d(sa);
    assert(d == b);
}

#ifdef WIN32
struct WSAInit {
    WSAInit() {
        WSADATA wsaData = { };
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

