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

#pragma once
#include "utility/serialize_fwd.h"
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

    SERIALIZE(packed);

private:
    Address (uint64_t x) : packed(x) {}

    uint64_t packed;
};

std::ostream& operator<<(std::ostream& os, const Address& a);

}} //namespaces

namespace std {
    template<> struct hash<beam::io::Address> {
        typedef beam::io::Address argument_type;
        typedef std::size_t result_type;

        result_type operator()(const argument_type& a) const noexcept {
            return std::hash<uint64_t>()(a.u64());
        }
    };
}

