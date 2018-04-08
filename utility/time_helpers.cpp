#include "time_helpers.h"
#include <chrono>
#include <stdio.h>
#include <time.h>

namespace beam {

uint64_t local_timestamp_msec() {
    using namespace std::chrono;
    // Ehhh. Never expose such an API
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

size_t format_timestamp(char* buffer, size_t bufferCap, const char* formatStr, uint64_t timestamp, bool formatMsec) {
    time_t seconds = (time_t)(timestamp/1000);
    struct tm tm;
    size_t nBytes = strftime(buffer, bufferCap, formatStr, localtime_r(&seconds, &tm));
    if (formatMsec && bufferCap - nBytes > 4) {
        snprintf(buffer + nBytes, 5, ".%03d", int(timestamp % 1000));
        nBytes += 4;
    }
    return nBytes;
}

} //namespace

