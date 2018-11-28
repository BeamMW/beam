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

#include "pow/stratum.h"
#include "core/ecc.h"
#include "utility/io/json_serializer.h"
#include "p2p/line_protocol.h"
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
        io::SerializedMsg m;

        LineProtocol lineProtocol(
            [](void*, size_t) -> bool { return false; },
            [&m](io::SharedBuffer&& fragment) { m.push_back(fragment); }
        );

        Result r("xxx", ResultCode(login_failed));
        append_json_msg(lineProtocol, r);
        io::SharedBuffer buf = io::normalize(m);
        LOG_DEBUG() << to_string(buf);
        Result x;
        auto code = parse_json_msg(buf.data, buf.size, x);
        if (code != 0) {
            LOG_ERROR() << "parse_json_msg failed, error=" << get_result_msg(code);
            ++nErrors;
        } else if (x.id != r.id || x.method != r.method) {
            LOG_ERROR() << "messages dont match";
        }
    } catch (const std::exception& e) {
        LOG_ERROR() << e.what();
        nErrors = 255;
    }

    return nErrors;
}

void gen_examples() {
    using namespace beam::stratum;

    Block::PoW pow;
    ECC::GenRandom(&pow.m_Nonce, Block::PoW::NonceType::nBytes);
    ECC::GenRandom(pow.m_Indices.data(), Block::PoW::nSolutionBytes);
    ECC::GenRandom(&pow.m_Difficulty.m_Packed, 4);
    Merkle::Hash hash;
    ECC::GenRandom(&hash.m_pData, 32);

    Login loginMsg("skjdb7343636gucgjdjgd");
    Job jobMsg("212", hash, pow);
    Cancel cancelMsg(212);
    Solution solMsg("212", pow);
    Result res1("login", login_failed);
    Result res2("212", solution_accepted);

    io::SerializedMsg m;

    LineProtocol packer(
        [](void*, size_t) -> bool { return false; },
        [&m](io::SharedBuffer&& fragment) {m.push_back(fragment);}
    );

    append_json_msg(packer, loginMsg);
    append_json_msg(packer, jobMsg);
    append_json_msg(packer, cancelMsg);
    append_json_msg(packer, solMsg);
    append_json_msg(packer, res1);
    append_json_msg(packer, res2);

    io::SharedBuffer buf = io::normalize(m);
    LOG_DEBUG() << to_string(buf);

    LineProtocol reader(
        [](void* data, size_t size) -> bool {
            LOG_DEBUG() << std::string((char*)data, size);
            return true;
        },
        [](io::SharedBuffer&& fragment) {}
    );

    reader.new_data_from_stream((void*)buf.data, buf.size);
}

} //namespace

int main() {
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = Logger::create(logLevel, logLevel);
    auto res = json_creation_test();
    gen_examples();
    return res;
}

