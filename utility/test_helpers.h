#pragma once
#include <chrono>
#include <stdint.h>

namespace beam { namespace helpers {

/// Simple thing for benchmarking pieces of tests
class StopWatch {
public:
    void start() {
        elapsed = 0;
        started = std::chrono::high_resolution_clock::now();
    }

    void stop() {
        auto diff = std::chrono::high_resolution_clock::now() - started;
        elapsed = std::chrono::duration_cast<std::chrono::microseconds>(diff).count();
    }

    uint64_t microseconds() const {
        return elapsed;
    }

    uint64_t milliseconds() const {
        return elapsed / 1000;
    }

    double seconds() const {
        return elapsed / 1000000.0;
    }

private:
    std::chrono::high_resolution_clock::time_point started;
    uint64_t elapsed;
};
}} //namespaces
