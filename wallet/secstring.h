#pragma once
#include "core/ecc.h"

namespace beam {

struct SecString {
    static const size_t MAX_SIZE = 128;

    size_t size=0;
    char data[MAX_SIZE];

    SecString(const SecString&) = delete;
    SecString& operator=(const SecString&) = delete;

    SecString(SecString&& ss) {
        assign(ss);
    }

    SecString& operator=(SecString&& ss) {
        assign(ss);
        return *this;
    }

    ~SecString() { erase(); }

    void erase() {
        if (size > 0) ECC::SecureErase(data, size);
    }

    void assign(void* p, size_t s) {
        erase();
        size = s > MAX_SIZE ? MAX_SIZE : s;
        if (size > 0) {
            memcpy(data, p, size);
            ECC::SecureErase(p, s);
        }
    }

    void assign(SecString& ss) {
        erase();
        size = ss.size;
        if (size > 0) {
            memcpy(data, ss.data, size);
            ss.erase();
        }
    }
};

} //namespace
