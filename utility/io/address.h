#pragma once
#include <string>
#include <iosfwd>
#include <functional>
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

    static Address from_u64(uint64_t x) {
        return Address(x);
    }

    Address() : packed(0) {}

    Address(const Address& a) : packed(a.packed) {}

    Address(const Address& a, uint16_t newPort)
        : packed(a.packed)
    {
        port(newPort);
    }

    Address(uint32_t a, uint16_t p) {
        packed = ((uint64_t)a << 16) + p;
    }

    Address(const sockaddr_in& sa) {
        packed = ((uint64_t)ntohl(sa.sin_addr.s_addr) << 16) + ntohs(sa.sin_port);
    }

    bool operator==(const Address& a) const {
        return packed == a.packed;
    }

    bool operator!=(const Address& a) const {
        return packed != a.packed;
    }

    bool operator<(const Address& a) const {
        return packed < a.packed;
    }

    bool empty() const {
        return packed == 0;
    }

    uint32_t ip() const {
        return (uint32_t)(packed >> 16);
    }

    uint64_t u64() const {
        return packed;
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

private:
    Address (uint64_t x) : packed(x) {}

    uint64_t packed;
};

std::ostream& operator<<(std::ostream& os, const Address& a);

}} //namespaces

namespace std {
    template<> struct hash<beam::io::Address> {
        size_t operator()(beam::io::Address a) const {
            return std::hash<uint64_t>()(a.u64());
        }
    };
}

