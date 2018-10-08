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

#include "fragment_writer.h"
#include <assert.h>

namespace beam { namespace io {

FragmentWriter::FragmentWriter(size_t fragmentSize, size_t headerSize, const OnNewFragment& callback) :
    _fragmentSize(fragmentSize),
    _headerSize(headerSize),
    _callback(callback)
{
    assert(_fragmentSize > _headerSize);
    assert(_callback);
}

void* FragmentWriter::write(const void *ptr, size_t size) {
    if (size == 0) return _cursor;
    void* where = _cursor;
    if (size <= _remaining) {
        memcpy(_cursor, ptr, size);
        _cursor += size;
        _remaining -= size;
        return where;
    }
    const uint8_t* p = (const uint8_t*)ptr;
    size_t sz = size;
    if (_remaining >= _headerSize) {
        memcpy(_cursor, p, _remaining);
        p += _remaining;
        sz -= _remaining;
        _cursor += _remaining;
    } else {
        where = 0;
    }
    while (sz > 0) {
        new_fragment();
        size_t n = _remaining < sz ? _remaining : sz;
        memcpy(_cursor, p, n);
        p += n;
        sz -= n;
        if (!where) where = _cursor;
        _cursor += n;
        _remaining -= n;
    }
    return where;
}

void FragmentWriter::finalize() {
    call();
    _msgBase = _cursor;
}

void FragmentWriter::call() {
    if (_msgBase != _cursor) {
        _callback(io::SharedBuffer(_msgBase, _cursor - _msgBase, _fragment));
    }
}

void FragmentWriter::new_fragment() {
    call();
    auto p = io::alloc_heap(_fragmentSize);
    _fragment = std::move(p.second);
    _msgBase = _cursor = (char*)p.first;
    _remaining = _fragmentSize;
}

}} //namespaces
