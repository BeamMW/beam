#include "address.h"
#include <stdlib.h>
#ifdef WIN32
    #include <Ws2tcpip.h>
#else
    #include <netdb.h>
#endif // WIN32

namespace beam { namespace io {

const Address Address::LOCALHOST = Address(0x7F000001, 0);

namespace {

uint32_t resolve_host(std::string&& host) {
    uint32_t ip = 0;

    addrinfo hint;
    memset(&hint, 0, sizeof(struct addrinfo));
    hint.ai_flags = AF_INET;
    hint.ai_socktype = SOCK_STREAM;

    addrinfo* ai=0;

    // NOTE: leaks memory, but only once
    int r = getaddrinfo(host.c_str(), 0, &hint, &ai);

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

std::ostream& operator << (std::ostream& s, const Address& v)
{
	uint32_t ipAddr = v.ip();

#define ADDR_FMT_STR "%u.%u.%u.%u:%u"
	char sz[_countof(ADDR_FMT_STR) + (3 - 2) * 4 + (5 - 2)];
	sprintf(sz, ADDR_FMT_STR, uint8_t(ipAddr >> 24), uint8_t(ipAddr >> 16), uint8_t(ipAddr >> 8), uint8_t(ipAddr), v.port());

	s << sz;
	return s;
}

}} //namespaces
