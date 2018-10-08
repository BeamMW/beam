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

#include "p2p/http_msg_creator.h"
#include "p2p/stratum.h"
#include "utility/helpers.h"
#include "utility/logger.h"

using namespace beam;

namespace {

std::string to_string(const io::SharedBuffer& buf) {
    if (buf.empty()) return std::string();
    return std::string((const char*)buf.data, buf.size);
}

int json_creation_test() {
    int nErrors = 0;

    using namespace beam::stratum;

    try {
        HttpMsgCreator packer(400);
        Response r(333, login, Error(message_corrupted));
        auto buf = create_json_msg(packer, r);
        LOG_DEBUG() << to_string(buf);
        Response x;
        int code = parse_json_msg(buf.data, buf.size, x);
        if (code != 0) {
            LOG_ERROR() << "parse_json_msg failed, error=" << get_error_msg(code);
            ++nErrors;
        } else if (x.error.code != r.error.code || x.id != r.id || x.method != r.method) {
            LOG_ERROR() << "messages dont match";
        }
    } catch (const std::exception& e) {
        LOG_ERROR() << e.what();
        nErrors = 255;
    }

    return nErrors;
}

} //namespace

int main() {
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = Logger::create(logLevel, logLevel);
    return json_creation_test();
}

