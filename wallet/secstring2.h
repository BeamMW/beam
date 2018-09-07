#pragma once
#include <stddef.h>
#include <string>

namespace beam {

class SecString {
public:
    static const size_t MAX_SIZE = 4096;
private:
    size_t _size=0;
    char* _data=0;

    void alloc();
    void dealloc();
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

    ~SecString() {
        erase();
        dealloc();
    }

    void erase();

    void assign(void* p, size_t s);

    void assign(const void* p, size_t s);

    void assign(SecString& ss);

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

};

} //namespace
