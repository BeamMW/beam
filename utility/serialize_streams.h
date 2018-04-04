#pragma once
#include <stdint.h>
#include <string.h>

namespace beam { namespace detail {

    struct SerializeOstream {

        // TODO replace by growing streams and shared chunk allocator

        enum { bufsize = 1024*100 };

        SerializeOstream()
            :cur(buf)
        {
            memset(buf, 0, bufsize);
        }

        size_t write(const void *ptr, const size_t size) {
            memcpy(cur, ptr, size);
            cur += size;
            *cur = 0;
            return size;
        }

        void clear() {
            cur = buf;
        }

        char buf[bufsize];
        char *cur;
    };

    struct SerializeIstream {
        SerializeIstream() : cur(0), end(0)
        {}

        void reset(const void *ptr, size_t size) {
            cur = (const char*)ptr;
            end = cur + size;
        }

        size_t read(void *ptr, const size_t size) {
            if ( cur+size > end )
                return 0;

            memcpy(ptr, cur, size);
            cur += size;
            return size;
        }

        size_t bytes_left() {
            return end - cur;
        }

        char peekch() const { return *cur; }
        char getch() { return *cur++; }
        void ungetch(char) { --cur; }

        const char *cur;
        const char *end;
    };

}} //namespaces
