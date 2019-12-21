#pragma once
#include "core/ecc_native.h"
#include <string_view>

namespace beam {

class SecString {
public:
    static const size_t MAX_SIZE = 4096;
private:
    size_t _size=0;
    char _data[MAX_SIZE] = {0};
public:

    SecString() = default;
    SecString(const SecString&) = delete;
    SecString& operator=(const SecString&) = delete;

    SecString(SecString&& ss) {
        assign(ss);
    }

    SecString(const std::string& sv) {
        assign(static_cast<const void*>(sv.data()), sv.size());
    }

    SecString& operator=(SecString&& ss) {
        assign(ss);
        return *this;
    }

    ~SecString() { erase(); }

    void erase() {
        if (_size > 0) ECC::SecureErase(_data, static_cast<uint32_t>(_size));
    }

    void assign(void* p, size_t s) {
        erase();
        _size = s > MAX_SIZE ? MAX_SIZE : s;
        if (_size > 0) {
            memcpy(_data, p, _size);
            ECC::SecureErase(p, static_cast<uint32_t>(s));
        }
    }

    void assign(const void* p, size_t s) {
        erase();
        _size = s > MAX_SIZE ? MAX_SIZE : s;
        if (_size > 0) {
            memcpy(_data, p, _size);
        }
    }

    void assign(SecString& ss) {
        erase();
        _size = ss._size;
        if (_size > 0) {
            memcpy(_data, ss._data, _size);
            ss.erase();
        }
    }

    size_t size() const
    {
        return _size;
    }

    const char* data() const
    {
        return _data;
    }

    bool empty() const
    {
        return _size == 0;
    }

    void push_back(char c)
    {
        assert(_size < MAX_SIZE);
        if (_size < MAX_SIZE) {
            _data[_size] = c;
            ++_size;
        }
    }

    void pop_back()
    {
        assert(!empty());
        if (!empty()) {
            --_size;
        }
    }

    ECC::NoLeak<ECC::uintBig> hash() const
    {
        ECC::NoLeak<ECC::uintBig> hash;

        ECC::Hash::Processor()
			<< Blob(data(), (uint32_t)size())
			>> hash.V;

        return hash;
    }
};

} //namespace
