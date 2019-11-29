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

#include "helpers.h"
#include <chrono>
#include <iostream>
#include <stdio.h>
#include <time.h>
#include <atomic>

#include "io/reactor.h"
#include "io/asyncevent.h"

#if defined __linux__
    #include <unistd.h>
    #include <termios.h>
    #include <sys/types.h>
    #include <sys/syscall.h>
    #include <sys/signal.h>
    #include <errno.h>
#elif defined _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <signal.h>
    #include <pthread.h>
    #include <errno.h>
    #include <unistd.h>
    #include <termios.h>
#endif

using namespace std;

namespace beam {

uint64_t local_timestamp_msec() {
    using namespace std::chrono;
    // Ehhh. Never expose such an API
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

size_t format_timestamp(char* buffer, size_t bufferCap, const char* formatStr, uint64_t timestamp, bool formatMsec) {
    time_t seconds = (time_t)(timestamp/1000);
    struct tm tm;
#ifdef WIN32
    localtime_s(&tm, &seconds);
    size_t nBytes = strftime(buffer, bufferCap, formatStr, &tm);
#else
    size_t nBytes = strftime(buffer, bufferCap, formatStr, localtime_r(&seconds, &tm));
#endif
    if (formatMsec && bufferCap - nBytes > 4) {
        snprintf(buffer + nBytes, 5, ".%03d", int(timestamp % 1000));
        nBytes += 4;
    }
    return nBytes;
}

char* to_hex(char* dst, const void* bytes, size_t size) {
    static const char digits[] = "0123456789abcdef";
    char* d = dst;

    const uint8_t* ptr = (const uint8_t*)bytes;
    const uint8_t* end = ptr + size;
    while (ptr < end) {
        uint8_t c = *ptr++;
        *d++ = digits[c >> 4];
        *d++ = digits[c & 0xF];
    }
    *d = '\0';
    return dst;
}

std::string to_hex(const void* bytes, size_t size) {
    char* buf = (char*)alloca(2 * size + 1);
    return std::string(to_hex(buf, bytes, size));
}

std::vector<uint8_t> from_hex(const std::string& str, bool* wholeStringIsNumber)
{
    size_t bias = (str.size() % 2) == 0 ? 0 : 1;
    assert((str.size() + bias) % 2 == 0);
    std::vector<uint8_t> res((str.size() + bias) >> 1);

    if (wholeStringIsNumber) *wholeStringIsNumber = true;

    for (size_t i = 0; i < str.size(); ++i)
    {
        auto c = str[i];
        size_t j = (i + bias) >> 1;
        res[j] <<= 4;
        if (c >= '0' && c <= '9')
        {
            res[j] += (c - '0');
        }
        else if (c >= 'a' && c <= 'f')
        {
            res[j] += 10 + (c - 'a');
        }
        else if (c >= 'A' && c <= 'F')
        {
            res[j] += 10 + (c - 'A');
        }
        else {
            if (wholeStringIsNumber) *wholeStringIsNumber = false;
            break;
        }
    }

    return res;
}


uint64_t get_thread_id() {
#if defined __linux__
    return syscall(__NR_gettid);
#elif defined _WIN32
    return GetCurrentThreadId();
#else
    uint64_t tid;
    pthread_threadid_np(NULL, &tid);
    return tid;
#endif
}

#ifndef _WIN32

namespace {

std::atomic<bool> g_quit = false;

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM || sig == SIGHUP)
        g_quit = true;
}

void install_signal(int sig) {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(sig, &sa, 0);
}

} //namespace

void block_signals_in_this_thread() {
    sigset_t sigmask;
    sigfillset(&sigmask);
    sigprocmask(SIG_BLOCK, &sigmask, 0);
}

void wait_for_termination(int nSec) {
    g_quit = false;

    install_signal(SIGTERM);
    install_signal(SIGINT);
    install_signal(SIGHUP);
    install_signal(SIGPIPE);

    struct timespec req, rem;
    if (nSec > 0)
        req.tv_sec = nSec;
    else
        req.tv_sec = 8640000;
    req.tv_nsec = 0;
    rem.tv_sec = 0;
    rem.tv_nsec = 0;
    for (;;) {
        int r = nanosleep(&req, &rem);
        if (g_quit)
            break;
        if (nSec > 0) {
            if (r == 0) break;
            req = rem;
            rem.tv_sec = 0;
            rem.tv_nsec = 0;
        }
    }
}

#else //_WIN32

void block_signals_in_this_thread() {
    // TODO ????
}

void wait_for_termination(int nSec) {
    // TODO
    // SetConsoleCtrlHandler, CreateEvent, WaitForSingleObject ????????
    ::Sleep(nSec);
}

#endif //_WIN32

void block_sigpipe() {
#ifndef WIN32
    sigset_t sigpipe_mask;
    sigemptyset(&sigpipe_mask);
    sigaddset(&sigpipe_mask, SIGPIPE);
    sigset_t saved_mask;
    if (pthread_sigmask(SIG_BLOCK, &sigpipe_mask, &saved_mask) == -1) {
        cerr << "pthread_sigmask failed\n";
        exit(1);
    }
#endif
}

std::future<void> do_thread_async(Functor&& functor, CompletionCallback&& callback)
{
    auto completedEvent = io::AsyncEvent::create(io::Reactor::get_Current(),
        [callback = move(callback)]()
    {
        callback();
    });

    return async(launch::async,
        [functor = move(functor), completedEvent]()
    {
        functor();
        completedEvent->post();
    });
}

} //namespace
