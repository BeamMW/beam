#pragma once
#include <string>
#include <stdint.h>
#include <string.h>
#ifdef WIN32
#include <winsock2.h>
#else
#include <netinet/ip.h>
#endif // WIN32

namespace beam { namespace io {

// IPv4 + port peer address
struct Address {
    static const Address LOCALHOST;

    static Address localhost() {
        return Address(LOCALHOST);
    }

    uint64_t packed=0;

    bool operator==(const Address& a) const {
        return packed == a.packed;
    }

    bool operator<(const Address& a) const {
        return packed < a.packed;
    }

    operator bool() const {
        return packed != 0;
    }

    Address() {}

    Address(uint32_t a, uint16_t p) {
        packed = ((uint64_t)a << 16) + p;
    }

    uint32_t ip() const {
        return (uint32_t)(packed >> 16);
    }

    Address& ip(uint32_t a) {
        packed = ((uint64_t)a << 16) + (packed & 0xFFFF);
        return *this;
    }

    uint16_t port() const {
        return (uint16_t)packed;
    }

    Address& port(uint16_t p) {
        packed = (packed & 0xFFFFFFFF0000) + p;
        return *this;
    }

    void fill_sockaddr_in(sockaddr_in& out) const {
        memset(&out, 0, sizeof(sockaddr_in));
        out.sin_family = AF_INET;
        out.sin_addr.s_addr = htonl(ip());
        out.sin_port = htons(port());
    }

    // NOTE: blocks
    bool resolve(const char* str);

    std::string str() const;
};

}} //namespaces

