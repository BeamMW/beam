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
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <future>
#include <stdint.h>
#include <assert.h>

namespace beam {

    class SecString;

// returns local timestamp in millisecond since the Epoch
uint64_t local_timestamp_msec();

// formatStr as for strftime (e.g. "%Y-%m-%d.%T"), if decimals==true, then .### milliseconds added
// returns bytes consumed
size_t format_timestamp(char* buffer, size_t bufferCap, const char* formatStr, uint64_t timestamp, bool formatMsec=true);

// formats current timestamp into std::string
inline std::string format_timestamp(const char* formatStr, uint64_t timestamp, bool formatMsec=true) {
    char buf[128];
    size_t n = format_timestamp(buf, 128, formatStr, timestamp, formatMsec);
    return std::string(buf, n);
}

// Converts bytes to base16 string, writes to dst buffer.
// dst must contain at least size*2 bytes + 1
char* to_hex(char* dst, const void* bytes, size_t size);

// Converts bytes to base16 string.
std::string to_hex(const void* bytes, size_t size);

// Converts hexdec string to vector of bytes, if wholeStringIsNumber!=0 it will contain true if the whole string is base16
std::vector<uint8_t> from_hex(const std::string& str, bool* wholeStringIsNumber=0);

/// Wraps member fn into std::function via lambda
template <typename R, typename ...Args, typename T> std::function<R(Args...)> bind_memfn(T* object, R(T::*fn)(Args...)) {
    return [object, fn](Args ...args) { return (object->*fn)(std::forward<Args>(args)...); };
}

/// Wrapper to bind member fn from inside this class methods
#define BIND_THIS_MEMFN(M) bind_memfn(this, &std::remove_pointer<decltype(this)>::type::M)

/// std::thread wrapper that spawns on demand
struct Thread {
    template <typename Func, typename ...Args> void start(Func func, Args ...args) {
        assert(!_thread);
        _thread = std::make_unique<std::thread>(func, std::forward<Args>(args)...);
    }

    operator bool() const { return _thread.get() != 0; }

    void join() {
        if (_thread) {
            _thread->join();

            // object may be reused after join()
            _thread.reset();
        }
    }

    virtual ~Thread() {
        // yes, must be joined first
        assert(!_thread);
    }

private:
    std::unique_ptr<std::thread> _thread;
};

/// returns current thread id depending on platform
uint64_t get_thread_id();

/// blocks all signals in calling thread
void block_signals_in_this_thread();

/// blocks sigpipe signals
void block_sigpipe();

/// waits for app termination
/// 1/ no more than nSec seconds if nSec > 0
/// 2/ until signal arrives otherwise
void wait_for_termination(int nSec);

/// Functor for async call
using Functor = std::function<void()>;
/// Functor which should be called when functor will be completed 
using CompletionCallback = std::function<void()>;

/// Performs functor in background thread, callback will be called in caller thread
std::future<void> do_thread_async(Functor&& functor, CompletionCallback&& callback);

} //namespace

