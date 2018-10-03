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

#include "bufferchain.h"
#include <assert.h>

namespace beam { namespace io {

void BufferChain::append(const void* data, size_t len, SharedMem guard, bool tryToJoin) {
    if (len == 0) return;
    _totalSize += len;
    if (tryToJoin && _totalSize != len) {

        assert(_iovecs.size() > 0 && _guards.size() == _iovecs.size());

        IOVec& iov = _iovecs.back();
        const void* ptr = guard.get();
        if (_guards.back().get() == ptr && (iov.data + iov.size == data)) {
            // joining to last fragment
            iov.size += len;
            return;
        }
    }
    _iovecs.emplace_back(data, len);
    _guards.push_back(std::move(guard));
}

void BufferChain::append(const BufferChain& bc) {
    if (bc.empty()) return;
    size_t n = bc.num_fragments();
    assert(n >= 1);
    const IOVec* frs = bc.iovecs();
    const SharedMem* gds = bc.guards();
    append(frs->data, frs->size, *gds, true);
    for (size_t i=1; i<n; ++i) {
        ++frs; ++gds;
        append(frs->data, frs->size, *gds, false);
    }
}

void BufferChain::advance(size_t nBytes) {
    if (nBytes == 0 || _totalSize == 0) return;
    if (nBytes >= _totalSize) {
        clear();
        return;
    }

    assert(num_fragments() > 0 && _guards.size() == _iovecs.size());

    _totalSize -= nBytes;
    size_t bytesRemaining = nBytes;
    for (size_t i=_index, sz=_iovecs.size(); i<sz; ++i) {
        size_t len = _iovecs[i].size;
        if (bytesRemaining >= len) {
            ++_index;
            _guards[i].reset();
            bytesRemaining -= len;
            if (!bytesRemaining) break;
        } else {
            _iovecs[i].advance(bytesRemaining);
            break;
        }
    }
    if (_index > REBASE_THRESHOLD) {
        rebase();
    }
}

void BufferChain::clear() {
    _iovecs.clear();
    _guards.clear();
    _totalSize = 0;
    _index = 0;
}

void BufferChain::rebase() {
    assert(_index > 0);

    size_t n = num_fragments();

    IOVec* ptr = _iovecs.data();
    memmove(ptr, ptr + _index, n * sizeof(IOVec));
    _iovecs.resize(n);

    for (size_t i=0; i<n; ++i) {
        _guards[i] = std::move(_guards[i + _index]);
    }
    _guards.resize(n);

    _index = 0;
}

}} //namespaces

