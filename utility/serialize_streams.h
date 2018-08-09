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
#include <stdexcept>
#include <stdint.h>
#include <string.h>
#include <vector>

namespace beam { namespace detail {

// Growing buffer serializer ostream
struct SerializeOstream {
    size_t write(const void *ptr, const size_t size) {
        if (size > 0) {
            size_t n = m_vec.size();
            m_vec.resize(n + size);
            memcpy(&m_vec.at(n), ptr, size);
        }
        return size;
    }

    void clear() {
        m_vec.clear();
    }

    std::vector<uint8_t> m_vec;
};

/// Sink for static buffer serializer
template <size_t BUFFER_SIZE> struct SerializeOstreamStatic {
    SerializeOstreamStatic()
        :cur(buf)
    {}

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
            raise_underflow();
        }

        memcpy(ptr, cur, size);
        cur += size;
        return size;
    }

    size_t bytes_left() {
        return end - cur;
    }

// Fns needed by Yas deserializer
    char peekch() const {
        if (cur >= end) raise_underflow();
        return *cur;
    }

    char getch() {
        if (cur >= end) raise_underflow();
        return *cur++;

    }
    void ungetch(char) { --cur; }

    /// Read cursor
    const char *cur;

    /// Buffer end
    const char *end;

    void raise_underflow() const {
        throw std::runtime_error("deserialize buffer underflow");
    }
};

}} //namespaces
