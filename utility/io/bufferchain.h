#pragma once
#include "buffer.h"
#include <vector>

namespace io {

class BufferChain {
public:
    BufferChain() : 
        _totalSize(0),
        _index(0)
    {}
    
    void append(const SharedBuffer& buf, bool tryToJoin=true) {
        append(buf.data, buf.size, buf.guard, tryToJoin);
    }

    void append(const void* data, size_t len, SharedMem guard, bool tryToJoin=true);

    size_t num_fragments() const {
        return _iovecs.size() - _index;
    }

    const iovec* fragments() const {
        return (const iovec*)_iovecs.data() + _index;
    }

    size_t size() const {
        return _totalSize;
    }

    void advance(size_t nBytes);

    void clear();
    
    bool empty() const {
        return _totalSize == 0;
    }

private:
    static const size_t REBASE_THRESHOLD = 128;
    
    void rebase();

    std::vector<IOVec> _iovecs;
    std::vector<SharedMem> _guards;
    
    /// Total size in bytes
    size_t _totalSize;
    
    /// Index of zero fragment in vectors
    size_t _index;
};
    
} //namespace
