#pragma once
#include <stdint.h>
#include <string.h>

namespace beam { namespace detail {

/// Sink for static buffer serializer
template <size_t BUFFER_SIZE> struct SerializeOstream {
    enum { bufsize = 1024*100 };

    SerializeOstream()
        :cur(buf)
    {
        memset(buf, 0, bufsize);
    }

    /// Called by serializer
    size_t write(const void *ptr, size_t size) {
        size_t avail = buf + BUFFER_SIZE - cur;
        size_t n = avail < size ? avail : size;
        if (n > 0) {
            memcpy(cur, ptr, n);
            cur += n;
        }
        return n;
    }

    /// Resets buffer
    void clear() {
        cur = buf;
    }

    /// Static buffer
    char buf[BUFFER_SIZE];

    /// Cursor
    char *cur;
};

/// Source for deserializer. References to contiguous byte buffer
struct SerializeIstream {
    /// Ctor. Initial state
    SerializeIstream() : cur(0), end(0)
    {}

    /// Resets to a new buffer
    void reset(const void *ptr, size_t size) {
        cur = (const char*)ptr;
        end = cur + size;
    }

    /// Reads from buffer
    size_t read(void *ptr, const size_t size) {
        if (cur + size > end) {
            // this will cause deserializer error, not enough data
            return 0;
        }

        memcpy(ptr, cur, size);
        cur += size;
        return size;
    }

    size_t bytes_left() {
        return end - cur;
    }

// Fns needed by Yas deserializer
    char peekch() const { return *cur; }
    char getch() { return *cur++; }
    void ungetch(char) { --cur; }

    /// Read cursor
    const char *cur;

    /// Buffer end
    const char *end;
};

}} //namespaces
