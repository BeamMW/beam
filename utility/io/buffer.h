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
#include <memory>
#include <vector>
#include <cstddef>
#include <stdint.h>
#include <string.h>
#ifdef WIN32
#include <Winsock2.h>
#else
#include <sys/uio.h>
#endif
namespace beam { namespace io {

/// IOVec casts to iovec, just holds const uint8_t* instead of void*
struct IOVec {
#ifdef WIN32
    size_t size;
    const uint8_t* data;
#else
    const uint8_t* data;
    size_t size;
#endif


    IOVec() : data(0), size(0)
    {}

    /// Assigns memory fragment
    IOVec(const void* _data, size_t _size) :
        data((const uint8_t*)_data), size(_size)
    {
#ifdef WIN32
        static_assert(
            sizeof(IOVec) == sizeof(WSABUF) &&
            offsetof(IOVec, data) == offsetof(WSABUF, buf) &&
            offsetof(IOVec, size) == offsetof(WSABUF, len),
            "IOVec must cast to iovec"
            );
#else
        static_assert(
            sizeof(IOVec) == sizeof(iovec) &&
            offsetof(IOVec, data) == offsetof(iovec, iov_base) &&
            offsetof(IOVec, size) == offsetof(iovec, iov_len),
            "IOVec must cast to iovec"
        );
#endif
    }

    /// Advances the pointer
    void advance(size_t nBytes) {
        if (nBytes >= size) {
            clear();
        } else {
            data += nBytes;
            size -= nBytes;
        }
    }

    void clear() {
        data = 0;
        size = 0;
    }

    bool empty() const {
        return (size == 0);
    }
};

/// Allows for sharing const memory regions
using SharedMem = std::shared_ptr<struct AllocatedMemory>;

/// Allocs shared memory from heap, throws on error
std::pair<uint8_t*, SharedMem> alloc_heap(size_t size);

struct SharedBuffer : IOVec {
    SharedMem guard;

    /// Empty buffer
    SharedBuffer() {}

    /// Creates a copy of data
    SharedBuffer(const void* _data, size_t _size) {
        assign(_data, _size);
    }

    void assign(const void* _data, size_t _size) {
        clear();
        if (_size) {
            auto p = alloc_heap(_size);
            memcpy(p.first, _data, _size);
            data = p.first;
            size = _size;
            guard = std::move(p.second);
        }
    }

    /// Assigns shared memory region
    SharedBuffer(const void* _data, size_t _size, SharedMem _guard) :
        IOVec(_data, _size),
        guard(std::move(_guard))
    {}

    /// Assigns shared memory region
    void assign(const void* _data, size_t _size, SharedMem _guard) {
        data = (const uint8_t*)_data;
        size = _size;
        guard = std::move(_guard);
    }

    void unique() {
        if (empty()) return;
        auto p = alloc_heap(size);
        memcpy(p.first, data, size);
        data = p.first;
        guard = std::move(p.second);
    }

    void clear() {
        IOVec::clear();
        guard.reset();
    }

    template<typename A> void serialize(A& a) const {
        a & size;
        if (size) {
            a.write(data, size);
        }
    }

    template<typename A> void serialize(A& a) {
        clear();
        size_t sz=0;
        a & sz;
        if (sz) {
            auto p = alloc_heap(sz);
            a.read(p.first, sz);
            data = p.first;
            size = sz;
            guard = std::move(p.second);
        }
    }
};

/// May have fragments...
using SerializedMsg = std::vector<SharedBuffer>;

/// Normalizes to 1 fragment and copies data.
/// This needed to detach some small and long-term message from large fragment (if makeUnique)
SharedBuffer normalize(const SerializedMsg& msg, bool makeUnique=false);

/// Maps whole file into memory, throws on errors
SharedBuffer map_file_read_only(const char* fileName);

}} //namespaces
