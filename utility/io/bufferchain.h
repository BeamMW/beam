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
#include "buffer.h"
#include <vector>

namespace beam { namespace io {

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

    void append(const BufferChain& bc);

    size_t num_fragments() const {
        return _iovecs.size() - _index;
    }

#ifdef WIN32
    const WSABUF* fragments() const {
        return (const WSABUF*)iovecs();
    }
#else
    const iovec* fragments() const {
        return (const iovec*)iovecs();
    }
#endif

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

    const IOVec* iovecs() const {
        return _iovecs.data() + _index;
    }

    const SharedMem* guards() const {
        return _guards.data() + _index;
    }

    void rebase();

    std::vector<IOVec> _iovecs;
    std::vector<SharedMem> _guards;

    /// Total size in bytes
    size_t _totalSize;

    /// Index of zero fragment in vectors
    size_t _index;
};

}} //namespaces
