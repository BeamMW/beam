#pragma once
#include <string>
#include <stdint.h>
#include <stddef.h>

namespace beam {

// returns local timestamp in millisecond since the Epoch
uint64_t local_timestamp_msec();

// formatStr as for strftime (e.g. "%Y-%m-%d.%T"), if decimals==true, then .### milliseconds added
// returns bytes consumed
size_t format_timestamp(char* buffer, size_t bufferCap, const char* formatStr, uint64_t timestamp, bool formatMsec=true);

inline std::string format_timestamp(const char* formatStr, uint64_t timestamp, bool formatMsec=true) {
    char buf[128];
    size_t n = format_timestamp(buf, 128, formatStr, timestamp, formatMsec);
    return std::string(buf, n);
}

// Converts bytes to base16 string, writes to dst buffer.
// dst must contain size*2 bytes + 1
char* to_hex(char* dst, const void* bytes, size_t size);

// Converts bytes to base16 string.
std::string to_hex(const void* bytes, size_t size);

} //namespace

