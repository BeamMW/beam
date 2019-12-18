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
#include "address.h"
#include <iostream>
#include <stdlib.h>
#ifdef WIN32
    #include <Ws2tcpip.h>
#else
    #include <arpa/inet.h>
    #include <netdb.h>
#endif // WIN32

namespace beam { namespace io {

const Address Address::LOCALHOST = Address(0x7F000001, 0);

namespace {

uint32_t resolve_host(std::string&& host) {
    uint32_t ip = 0;

    addrinfo hint;
    memset(&hint, 0, sizeof(struct addrinfo));
    hint.ai_family = AF_INET;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_protocol = IPPROTO_TCP;

    addrinfo* ai = nullptr;

    // NOTE: leaks memory, but only once
    int r = getaddrinfo(host.c_str(), nullptr, &hint, &ai);

    if (r == 0) {
        for (addrinfo* p = ai; p; p = p->ai_next) {
            if (p->ai_family == AF_INET) {
                ip = ntohl(((sockaddr_in*)(p->ai_addr))->sin_addr.s_addr);
                break;
            }
        }
    }

    if (ai) freeaddrinfo(ai);
    return ip;
}

} //namespace

bool Address::resolve(const char* str) {
    uint16_t p = 0;
    uint32_t a = 0;
    const char* colon = (const char*)strchr(str, ':');
    if (colon != 0) {
        p = (uint16_t)strtoul(colon+1, 0, 10);
        if (p <= 0) return false;
        a = resolve_host(std::string(str, colon));
    } else {
        a = resolve_host(str);
    }
    if (a) {
        packed = ((uint64_t)a << 16) + p;
        return true;
    }
    return false;
}

std::string Address::str() const {
    static const char* ipOnly = "%d.%d.%d.%d";
    static const char* ipAndPort = "%d.%d.%d.%d:%d";
    char buf[24];
    uint16_t p = port();
    uint32_t a = ip();
    if (p > 0) {
        snprintf(buf, 24, ipAndPort,
            a >> 24, (a >> 16) & 0xFF,(a >> 8) & 0xFF, a & 0xFF, p);
    } else {
        snprintf(buf, 24, ipOnly,
            a >> 24, (a >> 16) & 0xFF,(a >> 8) & 0xFF, a & 0xFF);
    }
    return std::string(buf);
}

std::ostream& operator<<(std::ostream& os, const Address& a) {
    uint32_t ip = a.ip();
    os << (ip >> 24) << '.' << ((ip >> 16) & 0xFF) << '.' << ((ip >> 8) & 0xFF) << '.' << (ip & 0xFF);
    uint16_t p = a.port();
    if (p) {
        os << ':' << p;
    }
    return os;
}

}} //namespaces
