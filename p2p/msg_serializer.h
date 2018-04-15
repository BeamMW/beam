#pragma once
#include "protocol.h"
#include "utility/serialize_streams.h"
#include "utility/io/buffer.h"
#include <vector>
#include <functional>
#include <assert.h>

namespace beam {

class FragmentWriter {
public:
    using OnNewFragment = std::function<void(io::SharedBuffer&& fragment)>;

    FragmentWriter(size_t fragmentSize, const OnNewFragment& callback) :
        _fragmentSize(fragmentSize), _callback(callback)
    {}

    void* write(const void *ptr, size_t size) {
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
        if (_remaining >= protocol::MsgHeader::SIZE) {
            memcpy(_cursor, p, _remaining);
            p += _remaining;
            sz -= _remaining;
            _cursor += _remaining;
        } else {
            where = 0;
        }
        while (sz > 0) {
            new_fragment();
            size_t n = std::min(_remaining, sz);
            memcpy(_cursor, p, n);
            p += n;
            sz -= n;
            if (!where) where = _cursor;
            _cursor += n;
            _remaining -= n;
        }
        return where;
    }

    void finalize() {
        call();
        _msgBase = _cursor;
    }

private:
    void call() {
        if (_msgBase != _cursor) {
            _callback(io::SharedBuffer(_msgBase, _cursor - _msgBase, _fragment));
        }
    }

    void new_fragment() {
        call();
        _fragment.reset(malloc(_fragmentSize), [](void* p) { free(p); });
        _msgBase = _cursor = (char*)_fragment.get();
        _remaining = _fragmentSize;
    }

    const size_t _fragmentSize;
    OnNewFragment _callback;
    io::SharedMem _fragment;
    char* _msgBase=0;
    char* _cursor=0;
    size_t _remaining=0;
};

class MsgSerializeOstream {
public:
    explicit MsgSerializeOstream(size_t fragmentSize) :
        _writer(
            fragmentSize,
            [this](io::SharedBuffer&& f) {
                _currentMsgSize += f.size;
                _fragments.push_back(std::move(f));
            }
        )
    {}

    void new_message(protocol::MsgType type) {
        assert(_currentMsgSize == 0 && _currentHeader == 0);
        _type = type;
        static const char dummy[protocol::MsgHeader::SIZE] = {0};
        _currentHeader = _writer.write(dummy, protocol::MsgHeader::SIZE);
    }

    size_t write(const void *ptr, size_t size) {
        assert(_currentHeader != 0);
        _writer.write(ptr, size);
    }

    void finalize(std::vector<io::SharedBuffer>& fragments) {
        assert(_currentHeader != 0);
        _writer.finalize();
        assert(_currentMsgSize >= protocol::MsgHeader::SIZE);
        protocol::MsgHeader(_type, uint32_t(_currentMsgSize - protocol::MsgHeader::SIZE)).write(_currentHeader);
        fragments.swap(_fragments);
        clear();
    }

    void clear() {
        _fragments.clear();
        _currentMsgSize = 0;
        _currentHeader = 0;
    }

private:
    FragmentWriter _writer;
    std::vector<io::SharedBuffer> _fragments;
    size_t _currentMsgSize=0;
    void* _currentHeader=0;
    protocol::MsgType _type=protocol::MsgType::null;

};

} //namespace
