#pragma once
#include <memory>
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
/// TODO use structure to unify mmap references etc
using SharedMem = std::shared_ptr<void>;

struct SharedBuffer : IOVec {
    SharedMem guard;

    /// Creates a copy of data
    SharedBuffer(const void* _data, size_t _size) {
        if (_size) {
            void* d = malloc(_size);
            // TODO throw if d==0
            memcpy(d, _data, _size);
            data = (uint8_t*)d;
            size = _size;
            guard.reset(d, [](void* p) { free(p); });
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

    void clear() {
        IOVec::clear();
        guard.reset();
    }
};

}} //namespaces
