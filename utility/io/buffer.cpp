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

#include "buffer.h"
#include <string>
#include <stdexcept>

#ifdef WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <sys/stat.h>
    #include <sys/mman.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

#include <assert.h>

namespace beam { namespace io {

struct AllocatedMemory {
    virtual ~AllocatedMemory() {}
};

struct HeapAllocatedMemory : AllocatedMemory {
    explicit HeapAllocatedMemory(size_t s) {
        size = s;
        if (size) {
            data = malloc(size);
            if (!data) throw std::runtime_error("HeapAllocatedMemory: out of memory");
        } else {
            data = 0;
        }
    }

    ~HeapAllocatedMemory() {
        if (data) free(data);
    }

    size_t size;
    void* data;
};

#ifdef WIN32

struct ReadOnlyMappedFileWin32 : AllocatedMemory {
    explicit ReadOnlyMappedFileWin32(const char* fileName) {
        fileHandle = CreateFileA(fileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_ALWAYS, NULL, NULL);
        if (fileHandle == INVALID_HANDLE_VALUE)
            throw std::runtime_error(std::string("ReadOnlyMappedFile: cannot open ") + fileName);
        DWORD fileSizeHigh = 0;
        size = GetFileSize(fileHandle, &fileSizeHigh);
        size += uint64_t(fileSizeHigh) << 32;
        if (size > 0) {
            mappingHandle = CreateFileMapping(fileHandle, NULL, PAGE_READONLY, 0, 0, NULL);
            if (mappingHandle == INVALID_HANDLE_VALUE) {
                CloseHandle(fileHandle);
                fileHandle = INVALID_HANDLE_VALUE;
                throw std::runtime_error(std::string("ReadOnlyMappedFile: cannot open mapping ") + fileName);
            }
            data = MapViewOfFile(mappingHandle, FILE_MAP_READ, 0, 0, size);
            if (!data) {
                CloseHandle(mappingHandle);
                fileHandle = INVALID_HANDLE_VALUE;
                throw std::runtime_error(std::string("ReadOnlyMappedFile: cannot view mapping ") + fileName);
            }
        }
    }

    ~ReadOnlyMappedFileWin32() {
        if (data) {
            UnmapViewOfFile(data);
        }
        if (mappingHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(mappingHandle);
        }
        if (fileHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(fileHandle);
        }
    }

    HANDLE fileHandle=INVALID_HANDLE_VALUE;
    HANDLE mappingHandle=INVALID_HANDLE_VALUE;
    uint64_t size=0;
    void* data=0;
};

#else

struct ReadOnlyMappedFile : AllocatedMemory {
    explicit ReadOnlyMappedFile(const char* fileName) {
        fd = open(fileName, O_RDONLY);
        if (fd < 0) throw std::runtime_error(std::string("ReadOnlyMappedFile: cannot open ") + fileName);
        struct stat sb;
        if (fstat(fd, &sb) == -1) {
            close(fd);
            fd = -1;
            throw std::runtime_error(std::string("ReadOnlyMappedFile: cannot fstat ") + fileName);
        }
        size = sb.st_size;
        if (size > 0) {
            data = mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0);
            if (data == MAP_FAILED) {
                close(fd);
                fd = -1;
                throw std::runtime_error(std::string("ReadOnlyMappedFile: cannot mmap ") + fileName);
            }
        }
    }

    ~ReadOnlyMappedFile() {
        if (data) {
            munmap(data, size);
        }
        if (fd >= 0) {
            close(fd);
        }
    }

    size_t size=0;
    void* data=0;
    int fd=-1;
};

#endif

std::pair<uint8_t*, SharedMem> alloc_heap(size_t size) {
    std::pair<uint8_t*, SharedMem> p;
    HeapAllocatedMemory* mem = new HeapAllocatedMemory(size);
    p.first = (uint8_t*)mem->data;
    p.second.reset(mem);
    return p;
}

SharedBuffer map_file_read_only(const char* fileName) {
#ifdef WIN32
    ReadOnlyMappedFileWin32* mem = new ReadOnlyMappedFileWin32(fileName);
#else
    ReadOnlyMappedFile* mem = new ReadOnlyMappedFile(fileName);
#endif
    SharedBuffer buf;
    buf.assign(mem->data, mem->size, SharedMem(mem));
    return buf;
}

SharedBuffer normalize(const SerializedMsg& msg, bool makeUnique) {
    size_t n = msg.size();
    if (n==0) return SharedBuffer();

    if (n==1) {
        if (makeUnique) {
            // copies
            return SharedBuffer(msg[0].data, msg[0].size);
        } else {
            return msg[0];
        }
    }

    size_t size = 0;
    for (const auto& fr : msg) {
        size += fr.size;
    }

    auto p = alloc_heap(size);
    uint8_t* ptr = p.first;

    for (const auto& fr : msg) {
        memcpy(ptr, fr.data, fr.size);
        ptr += fr.size;
    }

    return SharedBuffer(p.first, size, std::move(p.second));
}

}} //namespaces
