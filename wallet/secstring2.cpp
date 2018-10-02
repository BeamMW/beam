#include "secstring2.h"
#include "core/ecc.h"
#ifdef WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <unistd.h>
    #include <stdlib.h>
#endif

namespace beam {

void SecString::erase() {
    if (_size > 0) ECC::SecureErase(_data, static_cast<uint32_t>(_size));
}

void SecString::assign(void* p, size_t s) {
    erase();
    size_t newSize = s > MAX_SIZE ? MAX_SIZE : s;
    if (newSize > 0) {
        alloc();
        memcpy(_data, p, newSize);
        _size = newSize;
        ECC::SecureErase(p, static_cast<uint32_t>(s));
    } else {
        dealloc();
    }
}

void SecString::assign(const void* p, size_t s) {
    erase();
    size_t newSize = s > MAX_SIZE ? MAX_SIZE : s;
    if (newSize > 0) {
        alloc();
        memcpy(_data, p, newSize);
        _size = newSize;
    } else {
        dealloc();
    }
}

void SecString::assign(SecString& ss) {
    _size = ss._size;
    _data = ss._data;
    ss._data = 0;
    ss._size = 0;
}

void SecString::alloc() {
    if (_data) return;
    _size = 0;

#ifdef WIN32
    _data = (char*)VirtualAlloc(0, MAX_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!_data) throw std::runtime_error("SecString: VirtualAlloc failed");
    if (!VirtualLock(_data, MAX_SIZE)) {
        VirtualFree(_data, MAX_SIZE, MEM_RELEASE);
        _data = 0;
        throw std::runtime_error("SecString: VirtualLock failed");
    }
#else
    //_data = (char*)mmap(NULL, MAX_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_LOCKED, -1, 0);
    //if (_data == MAP_FAILED) throw std::runtime_error("SecString: mmap failed");

    int ret = posix_memalign((void**)&_data, (size_t)sysconf(_SC_PAGESIZE), MAX_SIZE);
    if (ret != 0 || !_data) {
        throw std::runtime_error("SecString: posix_memalign failed");
    }

    if (mlock(_data, MAX_SIZE) != 0) {
        free(_data);
        _data = 0;
        throw std::runtime_error("SecString: mlock failed");
    }

#endif
}

void SecString::dealloc() {
    if (!_data) return;
    _size = 0;

#ifdef WIN32
    VirtualUnlock(_data, MAX_SIZE);
    VirtualFree(_data, MAX_SIZE, MEM_RELEASE);
#else
    //munmap(_data, MAX_SIZE);

    munlock(_data, MAX_SIZE);
    free(_data);

#endif

    _data = 0;
}

} //namespace

