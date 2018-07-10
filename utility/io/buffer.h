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
/// TODO use structure to unify mmap references etc
using SharedMem = std::shared_ptr<void>;

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
            void* d = alloc_data(_size);
            memcpy(d, _data, _size);
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
            void* d = alloc_data(sz);
            a.read(d, sz);
        }
    }

private:
    void* alloc_data(size_t _size) {
        void* d = malloc(_size);
        if (d==0) throw std::runtime_error("out of memory");
        data = (uint8_t*)d;
        size = _size;
        guard.reset(d, [](void* p) { free(p); });
        return d;
    }
};

/// May have fragments...
using SerializedMsg = std::vector<SharedBuffer>;

/// Normalizes to 1 fragment and copies data.
/// This needed to detach some small and long-term message from large fragment (if makeUnique)
SharedBuffer normalize(const SerializedMsg& msg, bool makeUnique=false);

}} //namespaces
