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

namespace beam::wallet {
    unsigned days2sec(unsigned days)
    {
        return 60 * 60 * 24 * days;
    }

     std::string msec2readable(unsigned sec)
     {
        return sec2readable(sec / 10000);
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
}
