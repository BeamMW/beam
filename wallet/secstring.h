#pragma once
#include "core/ecc.h"
#include <string_view>

namespace beam {

class SecString {
public:
    static const size_t MAX_SIZE = 128;
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
        assign(static_cast<const void*>(sv.data()), sv.size() + 1 );
    }

    SecString& operator=(SecString&& ss) {
        assign(ss);
        return *this;
    }

    ~SecString() { erase(); }

    void erase() {
        if (_size > 0) ECC::SecureErase(_data, _size);
    }

    void assign(void* p, size_t s) {
        erase();
        _size = s > MAX_SIZE ? MAX_SIZE : s;
        if (_size > 0) {
            memcpy(_data, p, _size);
            ECC::SecureErase(p, s);
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
};

} //namespace
