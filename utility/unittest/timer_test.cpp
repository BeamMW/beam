#include "utility/io/coarsetimer.h"
#include <set>

#define LOG_VERBOSE_ENABLED 1
#include "utility/logger.h"

using namespace beam;
using namespace beam::io;
using namespace std;

Reactor::Ptr reactor;

void timer_test() {
    reactor = Reactor::create();
    Timer::Ptr timer = Timer::create(reactor);
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
        reactor,
        50, //msec
        on_coarse_timer
    );
        
    for (uint64_t i=1; i<111; ++i) {
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
    LoggerConfig lc;
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    lc.consoleLevel = logLevel;
    lc.flushLevel = logLevel;
    auto logger = Logger::create(lc);
    timer_test();
    coarsetimer_test();
}


