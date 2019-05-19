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

#include "log_rotation.h"
#include "helpers.h"
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <map>
#include <vector>

namespace beam {

LogRotation::LogRotation(io::Reactor& reactor, unsigned rotatePeriodSec, unsigned cleanPeriodSec) :
    _cleanPeriodSec(cleanPeriodSec),
    _logRotateTimer(io::Timer::create(reactor))
{
    _logRotateTimer->start(
        rotatePeriodSec * 1000, true,
        BIND_THIS_MEMFN(on_timer)
    );
}

void LogRotation::on_timer() {
    Logger::get()->rotate();
    auto currentTime = time_t(local_timestamp_msec() / 1000);
    while (!_expiration.empty()) {
        auto it = _expiration.begin();
        if (it->first > currentTime)
            break;
        boost::filesystem::remove_all(it->second);
        _expiration.erase(it);
    }
    _expiration.insert( { currentTime + _cleanPeriodSec, Logger::get()->get_current_file_name() } );
}

static void clean_old_logfiles_2(const std::string& directory, const std::string& prefix, unsigned cleanPeriodSec) {
    namespace fs = boost::filesystem;

    static const unsigned SECS_IN_DAY = 86400;

    // day -> files
    using DayBuckets = std::map<unsigned, std::vector<fs::path>>;
    DayBuckets days;

    auto now = time_t(local_timestamp_msec() / 1000);

    try {
        fs::directory_iterator it(fs::system_complete(directory));
        fs::directory_iterator end;
        for (; it != end; ++it) {
            auto p = it->path();
            if (!fs::is_regular_file(p) ||
                !boost::starts_with(p.filename().string(), prefix) ||
                !(p.extension().string() == ".log")
            ) continue;
            auto dayAgo = unsigned( (now - fs::last_write_time(p)) / SECS_IN_DAY );
            days[dayAgo].push_back(p);
        }

        unsigned cleanPeriodDays = cleanPeriodSec / SECS_IN_DAY; // will take only working days into account, sex intraday

        unsigned day = 0;
        for (const auto& x : days) {
            if (day++ < cleanPeriodDays) continue; // skip specified working days
            for (auto& p : x.second) {
                LOG_INFO() << "removing old log file " << p;
                boost::filesystem::remove_all(p);
            }
        }

    } catch (const boost::exception& e) {
        LOG_ERROR() << "cleanup old logs failed, " << boost::diagnostic_information(e);
    } catch (...) {
        //~
    }
}

void clean_old_logfiles(const std::string& directory, const std::string& prefix, unsigned cleanPeriodSec) {
    clean_old_logfiles_2(directory, prefix, cleanPeriodSec);

    /*
     * maybe we'll return this
     *
    namespace fs = boost::filesystem;

    try {
        auto expiration = time_t(local_timestamp_msec() / 1000) - cleanPeriodSec;
        fs::directory_iterator it(fs::system_complete(directory));
        fs::directory_iterator end;
        for (; it != end; ++it) {
            auto p = it->path();
            if (fs::last_write_time(p) > expiration || !fs::is_regular_file(p))
                continue;
            if (boost::starts_with(p.filename().string(), prefix) && p.extension().string() == ".log") {
                boost::filesystem::remove_all(p);
            }
        }
    } catch (...) {
        //~
    }
     */
}

} //namespace
