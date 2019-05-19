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

#include "utility/io/coarsetimer.h"
#include <set>

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 1
#endif
#include "utility/logger.h"

using namespace beam;
using namespace beam::io;
using namespace std;

Reactor::Ptr reactor;

void timer_test() {
    reactor = Reactor::create();
    Timer::Ptr timer = Timer::create(*reactor);
    int countdown = 5;

    LOG_DEBUG() << "setting up one-shot timer";
    timer->start(
        300,
        false,
        [&countdown, &timer] {
            LOG_DEBUG() << "starting periodic timer";
            timer->start(
                111,
                true,
                [&countdown] {
                    LOG_DEBUG() << countdown;
                    if (--countdown == 0)
                    reactor->stop();
                }
            );
        }
    );

    LOG_DEBUG() << "Starting";
    reactor->run();
    LOG_DEBUG() << "Stopping";
}

CoarseTimer::Ptr ctimer;
set<uint64_t> usedIds;

void on_coarse_timer(uint64_t id) {
    assert(usedIds.count(id) == 0);
    usedIds.erase(id);
    // will cancel such ids
    assert(id % 3 != 0);
    LOG_DEBUG() << "id=" << id;
    if (id > 200) {
        reactor->stop();
    } else {
        if (id < 30) {
            // setting timer with the same id from inside callback
            ctimer->set_timer(77, id);
        } else if (id < 120) {
            // setting timer with different id
            ctimer->set_timer(123, id + 120);
        }
        //cancelling timer from inside callback
        ctimer->cancel(id + 1);
    }
}

void coarsetimer_test() {
    reactor = Reactor::create();
    ctimer = CoarseTimer::create(
        *reactor,
        50, //msec
        on_coarse_timer
    );

    for (unsigned i=1; i<111; ++i) {
        ctimer->set_timer(200 + i*3, i);
    }

    for (uint64_t i=0; i<111; i+=3) {
        ctimer->cancel(i);
    }

    LOG_DEBUG() << "Starting";
    reactor->run();
    LOG_DEBUG() << "Stopping";
}

int main() {
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = Logger::create(logLevel, logLevel);
    timer_test();
    coarsetimer_test();
}

