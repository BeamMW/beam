#pragma once
#include <stdint.h>
#include <string.h>
#include <vector>

namespace beam { namespace detail {

    struct SerializeOstream {

        size_t write(const void *ptr, const size_t size) {
			size_t n = m_vec.size();
			m_vec.resize(n + size);
			memcpy(&m_vec.at(n), ptr, size);
            return size;
        }

        void clear() {
			m_vec.clear();
        }

		std::vector<uint8_t> m_vec;
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
