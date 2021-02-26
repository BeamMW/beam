// Copyright 2020 The Beam Team
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
#include <chrono>
#include <sstream>
#include "utils.h"
#include "utility/logger.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <execinfo.h>
#include <csignal>
#endif

namespace beam::wallet {
    unsigned days2sec(unsigned days)
    {
        return 60 * 60 * 24 * days;
    }

     std::string msec2readable(unsigned sec)
     {
        return sec2readable(sec / 1000);
     }

    std::string sec2readable(unsigned sec)
    {
        using namespace std;
        using namespace std::chrono;
        typedef duration<int, ratio<60 * 60 * 24>> days;

        seconds secs(sec);
        std::ostringstream stream;

        auto d = duration_cast<days>(secs);
        secs -= d;

        auto h = duration_cast<hours>(secs);
        secs -= h;

        auto m = duration_cast<minutes>(secs);
        secs -= m;

        if (d.count())
        {
            stream << d.count() << " days";
        }

        if (h.count())
        {
            if (stream.str().length()) stream << " ";
            stream << h.count() << " hours";
        }


        if (m.count())
        {
            if (stream.str().length()) stream << " ";
            stream << m.count() << " minutes";
        }

        if (secs.count())
        {
            if (stream.str().length()) stream << " ";
            stream << secs.count() << " seconds";
        }

        return stream.str();
    }

    unsigned getAliveLogInterval()
    {
        #ifndef NDEBUG
        return 1000 * 60 * 5; // 5 minutes
        #else
        return 1000 * 60 * 1; // 1 minute
        #endif
    }

    void logAlive(const std::string& name)
    {
        LOG_INFO() << "== Hey! " << name << " is sill alive ==";
    }

    unsigned getCurrentPID()
    {
        #ifdef _WIN32
        return GetCurrentProcessId();
        #else
        return static_cast<unsigned>(getpid());
        #endif
    }

    #ifdef _WIN32
    void activateCrashLog () {
    }
    #else

    typedef void (*SigHandler)(int,siginfo_t *,void *) ;
    void setHandler(SigHandler handler)
    {
        struct sigaction action = {};
        action.sa_flags = SA_SIGINFO;
        action.sa_sigaction = handler;

        if (sigaction(SIGSEGV, &action, nullptr) == -1) {
            perror("sigsegv: sigaction");
            _exit(1);
        }

        if (sigaction(SIGABRT, &action, nullptr) == -1) {
            perror("sigabrt: sigaction");
            _exit(1);
        }
    }

    void handleSIGS(int signo, siginfo_t *info, void *extra)
    {
        // print some info
        fprintf(stderr, "Sig %d received", signo);
        fprintf(stderr, " si_addr=%p", info->si_addr);
        auto pContext = reinterpret_cast<ucontext_t*>(extra);
        fprintf(stderr, " reg_ip=%p\n", reinterpret_cast<void*>(pContext->uc_mcontext.gregs[REG_RIP]));

        // print backtrace
        const size_t trCnt = 50;
        void *array[trCnt];
        size_t size = backtrace(array, trCnt);

        fprintf(stderr, "Backtrace\n");
        backtrace_symbols_fd(array, size, STDERR_FILENO);

        // generate crash dump
        setHandler(reinterpret_cast<SigHandler>(SIG_DFL));
    }

    void activateCrashLog () {
        setHandler(handleSIGS);
    }
    #endif
}
