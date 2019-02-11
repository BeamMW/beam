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
#include "io/timer.h"
#include <map>

namespace beam {

/// Rotates logs by timer and cleans old files
class LogRotation {
public:
    LogRotation(io::Reactor& reactor, unsigned rotatePeriodSec, unsigned cleanPeriodSec);

    ~LogRotation() = default;

private:
    void on_timer();

    const unsigned _cleanPeriodSec;
    io::Timer::Ptr _logRotateTimer;
    std::map<time_t, Logger::FileNameType> _expiration;
};

void clean_old_logfiles(const std::string& directory, const std::string& prefix, unsigned cleanPeriodSec);

} //namespace
