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

#include "utility/logger_checkpoints.h"
#include "utility/helpers.h"
#include <thread>

using namespace beam;

struct XXX {
    int z = 333;
};

std::ostream& operator<<(std::ostream& os, const XXX& xxx) {
    os<< "XXX={" << xxx.z << "}";
    return os;
}

static size_t custom_header_formatter(char* buf, size_t maxSize, const char* timestampFormatted, const LogMessageHeader& header) {
    return snprintf(buf, maxSize, "%c %s ", loglevel_tag(header.level), timestampFormatted);
}

void test_logger_1() {
    auto logger = Logger::create(LOG_LEVEL_WARNING, LOG_LEVEL_DEBUG, LOG_LEVEL_WARNING, "Zzzzz");
    logger->set_header_formatter(custom_header_formatter);
    logger->set_time_format("%T", false);

    LOG_CRITICAL() << "Let's die";
    LOG_ERROR() << "Not so bad at all, here is " << format_timestamp("%y-%m-%d.%T", local_timestamp_msec());
    LOG_WARNING() << "Don't be afraid: " << 223322223;
    XXX xxx;
    LOG_INFO() << xxx;
    LOG_DEBUG() << "YYY";
    LOG_VERBOSE() << "ZZZ";
}

void test_ndc_1() {
    auto logger = Logger::create(LOG_LEVEL_WARNING, LOG_LEVEL_DEBUG, LOG_LEVEL_WARNING, "Zzzzz");
    logger->set_header_formatter(custom_header_formatter);
    CHECKPOINT_CREATE (6);
    CHECKPOINT_ADD() << "ssss" << 333 << 555;
    CHECKPOINT_CREATE (6);
    CHECKPOINT_ADD() << "zzz" << 777 << 888;
    std::string zzz("Blablabla");
    CHECKPOINT_ADD() << &zzz; // constraint: objects captured by ptr in checkpoints
    CHECKPOINT (3333, 44444, 5555, 66666, 77777, 88888);
    LOG_ERROR() << FlushAllCheckpoints();
}

void test_ndc_2(bool exc)
{
    auto logger = Logger::create();
    logger->set_header_formatter(custom_header_formatter);
    {
        std::thread::id threadId = std::this_thread::get_id();
        CHECKPOINT("WorkerThread:", &threadId); // constraint: objects captured by ptr in checkpoints
        CHECKPOINT("Processing I/O");
        {
            CHECKPOINT("Request from client ID:", 246);
            {
                if (exc) throw("xxx");
                CHECKPOINT("Sending a file. Path:", "C:\\Blablabla.bin");
                {
                    LOG_ERROR() << "Can't open file. Error:" << EACCES << FlushAllCheckpoints();
                }
            }
        }
    }
}

int main() {
    test_logger_1();
    test_ndc_1();
    test_ndc_2(false);
    try {
        test_ndc_2(true);
    }
    catch(...) {}
}
