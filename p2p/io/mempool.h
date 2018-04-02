#pragma once
#include <vector>
#include <stdlib.h>
#include <string.h>

namespace io {
    
template <class T, size_t DATA_SIZE> class MemPool {
public:
    explicit MemPool(size_t maxSize) :
        _maxSize(maxSize)
    {
        _pool.reserve(maxSize);
    }

    ~MemPool() {
        for (T* h: _pool) {
            free(h);
        }
    }

    T* alloc() {
        T* h = 0;
        if (!_pool.empty()) {
            h = _pool.back();
            _pool.pop_back();
        } else {
            h = (T*)calloc(1, DATA_SIZE);
        }
        return h;
    }

    void release(T* h) {
        if (_pool.size() > _maxSize) {
            free(h);
        } else {
            memset(h, 0, DATA_SIZE);
            _pool.push_back(h);
        }
    }

private:
    using Pool = std::vector<T*>;

    Pool _pool;
    size_t _maxSize;
};
    
} //namespace

