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
#include "p2p/http_msg_creator.h"
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
        Result r("xxx", ResultCode(login_failed));
        io::SerializedMsg m;
        append_json_msg(m, packer, r);
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
    Job jobMsg(212, hash, pow);
    Cancel cancelMsg(212);
    Solution solMsg(212, pow);
    Result res1("login", login_failed);
    Result res2("212", solution_accepted);

    io::SharedBuffer sep("\n", 1);

    HttpMsgCreator packer(2000);
    io::SerializedMsg m;
    append_json_msg(m, packer, loginMsg);
    m.push_back(sep);
    append_json_msg(m, packer, jobMsg);
    m.push_back(sep);
    append_json_msg(m, packer, cancelMsg);
    m.push_back(sep);
    append_json_msg(m, packer, solMsg);
    m.push_back(sep);
    append_json_msg(m, packer, res1);
    m.push_back(sep);
    append_json_msg(m, packer, res2);

    io::SharedBuffer buf = io::normalize(m);
    LOG_DEBUG() << to_string(buf);
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

