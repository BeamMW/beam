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

#include "utility/io/reactor.h"
#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif
#include "utility/logger.h"
#include <future>
#include <iostream>

using namespace beam;
using namespace beam::io;
using namespace std;

void reactor_start_stop() {
    Reactor::Ptr reactor = Reactor::create();

    auto f = std::async(
        std::launch::async,
        [reactor]() {
            this_thread::sleep_for(chrono::microseconds(300000));
            //usleep(300000);
            LOG_DEBUG() << "stopping reactor from foreign thread...";
            reactor->stop();
        }
    );

    LOG_DEBUG() << "starting reactor...";;
    reactor->run();
    LOG_DEBUG() << "reactor stopped";

    f.get();
}

void error_codes_test() {
    std::string unknown_descr = error_descr(ErrorCode(1005000));
    std::string str;
#define XX(code, _) \
    str = format_io_error("", "", 0, EC_ ## code); \
    LOG_VERBOSE() << str; \
    assert(str.find(unknown_descr) == string::npos);

    UV_ERRNO_MAP(XX)
#undef XX
}

int main() {
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = Logger::create(logLevel, logLevel);
    reactor_start_stop();
    error_codes_test();
}
