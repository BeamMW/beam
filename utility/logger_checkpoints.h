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
#include "logger.h"
#include <tuple>

#define COMBINE1(X,Y) X##Y
#define COMBINE(X,Y) COMBINE1(X,Y)
#define NUM_ARGS(...) std::tuple_size<decltype(std::make_tuple(__VA_ARGS__))>::value

#define CHECKPOINT_CREATE(SIZE) \
    beam::CheckpointData<SIZE> COMBINE(checkpointData,__LINE__); \
    beam::Checkpoint COMBINE(checkpoint,__LINE__)(COMBINE(checkpointData,__LINE__).items, SIZE, __FILE__, __LINE__, __FUNCTION__)

#define CHECKPOINT_ADD() assert(beam::current_checkpoint()); *beam::current_checkpoint()

#define CHECKPOINT(...) \
    beam::CheckpointData<NUM_ARGS(__VA_ARGS__)> COMBINE(checkpointData,__LINE__); \
    beam::Checkpoint COMBINE(checkpoint,__LINE__)(COMBINE(checkpointData,__LINE__).items, NUM_ARGS(__VA_ARGS__), __FILE__, __LINE__, __FUNCTION__); \
    COMBINE(checkpoint,__LINE__).push(__VA_ARGS__)

namespace beam {

class Checkpoint* current_checkpoint();

namespace detail {
    union Data {
        const void* ptr;
        const char* cstr;
        uint64_t value;
    };

    typedef void (*flush_fn)(LogMessage& m, Data* d);

    template<class T> static inline void flush_pointer(LogMessage& m, Data* d) {
        T* t = (T*)d->ptr;
        m << t << ' ';
    }

    template<class T> static inline void flush_value(LogMessage& m, Data* d) {
        T* t = (T*)d;
        m << *t << ' ';
    }

    static inline void flush_cstr(LogMessage& m, Data* d) {
        m << d->cstr << ' ';
    }

    struct CheckpointItem {
        flush_fn fn;
        Data data;
    };

} //namespace

template <size_t MAX_ITEMS> struct CheckpointData {
    detail::CheckpointItem items[MAX_ITEMS];
};

class Checkpoint {
    LogMessageHeader _header;
    detail::CheckpointItem* _items;
    detail::CheckpointItem* _ptr;
    size_t _maxItems;
    Checkpoint* _next;
    Checkpoint* _prev;

    void flush_from_here();
    void flush_to(LogMessage* to);

public:
    Checkpoint(detail::CheckpointItem* items, size_t maxItems, const char* file, int line, const char* function);

    void flush(LogMessage* to);

    ~Checkpoint();

    template <typename T> Checkpoint& operator<<(T value) {
        assert(_ptr < _items + _maxItems);
        if constexpr (std::is_same<T, const char*>::value) {
            _ptr->fn = detail::flush_cstr;
            _ptr->data.ptr = value;
        } else if constexpr (std::is_pointer<T>::value) {
            _ptr->fn = detail::flush_pointer<typename std::remove_pointer<T>::type>;
            _ptr->data.ptr = value;
        } else if constexpr (std::is_arithmetic<T>::value) {
            _ptr->fn = detail::flush_value<T>;
            T* pvalue = reinterpret_cast<T*>(&(_ptr->data.value));
            *pvalue = value;
        } else {
            assert(false && "Non-supported type in checkpoint, use pointers");
        }

        ++_ptr;
        return *this;
    }

    void push() {}

    template <typename T> void push(T a) {
        *this << a;
    }

    template <typename T, typename... Args> void push(T a, Args... args) {
        push(a);
        push(args...);
    }
};



} //namespace
