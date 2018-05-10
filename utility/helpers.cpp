#include "helpers.h"
#include <chrono>
#include <stdio.h>
#include <time.h>

#if defined __linux__
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/syscall.h>
#elif defined _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <pthread.h>
#endif

namespace beam {

uint64_t local_timestamp_msec() {
    using namespace std::chrono;
    // Ehhh. Never expose such an API
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

size_t format_timestamp(char* buffer, size_t bufferCap, const char* formatStr, uint64_t timestamp, bool formatMsec) {
    time_t seconds = (time_t)(timestamp/1000);
    struct tm tm;
#ifdef WIN32
    localtime_s(&tm, &seconds);
    size_t nBytes = strftime(buffer, bufferCap, formatStr, &tm);
#else
    size_t nBytes = strftime(buffer, bufferCap, formatStr, localtime_r(&seconds, &tm));
#endif
    if (formatMsec && bufferCap - nBytes > 4) {
        snprintf(buffer + nBytes, 5, ".%03d", int(timestamp % 1000));
        nBytes += 4;
    }
    return nBytes;
}

char* to_hex(char* dst, const void* bytes, size_t size) {
    static const char digits[] = "0123456789abcdef";
    char* d = dst;

    const uint8_t* ptr = (const uint8_t*)bytes;
    const uint8_t* end = ptr + size;
    while (ptr < end) {
        uint8_t c = *ptr++;
        *d++ = digits[c >> 4];
        *d++ = digits[c & 0xF];
    }
    *d = '\0';
    return dst;
}

std::string to_hex(const void* bytes, size_t size) {
    char* buf = (char*)alloca(2 * size + 1);
    return std::string(to_hex(buf, bytes, size));
}

uint64_t get_thread_id() {
#if defined __linux__
    return syscall(__NR_gettid);
#elif defined _WIN32
    return GetCurrentThreadId();
#else
    uint64_t tid;
    pthread_threadid_np(NULL, &tid);
    return tid;
#endif
}

} //namespace
