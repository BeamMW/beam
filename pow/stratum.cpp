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

#include "stratum.h"
#include "p2p/http_msg_creator.h"
#include "nlohmann/json.hpp"
#include "utility/helpers.h"
#include "utility/logger.h"

using json = nlohmann::json;

namespace beam::stratum {

static const std::string method_strings[] = {
#define METHOD_STR(_, M, __) std::string(#M),
    STRATUM_METHODS(METHOD_STR)
#undef METHOD_STR
};

std::string get_method_str(Method methodId) {
    if (methodId == 0 || methodId >= METHODS_END) return std::string();
    return method_strings[methodId];
}

Method get_method(const std::string& str) {
    if (str.empty()) return null_method;
    for (int i=0; i<METHODS_END; ++i) {
        if (str == method_strings[i]) return Method(i);
    }
    return null_method;
}

std::string get_result_msg(int code) {
    if (code == 0) return std::string();
    switch (code) {
#define R_MESSAGE(code, _, message) case code: return message;
        STRATUM_RESULTS(R_MESSAGE)
#undef R_MESSAGE
        default: break;
    }
    return "unknown";
}

namespace {

#define DEF_LABEL(label) static const std::string l_##label (#label)
    DEF_LABEL(jsonrpc);
    DEF_LABEL(id);
    DEF_LABEL(method);
    DEF_LABEL(description);
    DEF_LABEL(code);
    DEF_LABEL(message);
    DEF_LABEL(api_key);
    DEF_LABEL(input);
    DEF_LABEL(difficulty);
    DEF_LABEL(nonce);
    DEF_LABEL(output);
#undef DEF_LABEL

ResultCode parse_json(const void* buf, size_t bufSize, json& o) {
    if (bufSize == 0) return message_corrupted;
    const char* bufc = (const char*)buf;
    try {
        o = json::parse(bufc, bufc + bufSize);
    } catch (const std::exception& e) {
        LOG_ERROR() << "json parse: " << e.what() << "\n" << std::string(bufc, bufc + (bufSize > 1024 ? 1024 : bufSize));
        return message_corrupted;
    }
    return no_error;
}

ResultCode parse_message(const json& o, Message& m) {
    m.id = o[l_id];
    if (m.id.empty()) return empty_id;
    m.method_str = o[l_method];
    m.method = get_method(m.method_str);
    if (m.method == 0) return unknown_method;
    return no_error;
}

void append_base(json& o, const Message& m) {
    o[l_jsonrpc] = "2.0";
    o[l_id] = m.id;
    o[l_method] = m.method_str;
}

} //namespace

bool append_json_msg(io::SerializedMsg& out, HttpMsgCreator& packer, const Login& m) {
    json o;
    append_base(o, m);
    o[l_api_key] = m.api_key;
    return append_json_msg(out, packer, o);
}

ResultCode parse_json_msg(const void* buf, size_t bufSize, Login& m) {
    json o;
    ResultCode r = parse_json(buf, bufSize, o);
    if (r != 0) return r;
    r = parse_message(o, m);
    if (r != 0) return r;
    m.api_key = o[l_api_key];
    return no_error;
}

Job::Job(uint64_t _id, const Merkle::Hash& _input, const Block::PoW& _pow) :
    Message(std::to_string(_id), job),
    difficulty(_pow.m_Difficulty.m_Packed)
{
    char buf[72];
    input = to_hex(buf, _input.m_pData, 32);
}

bool append_json_msg(io::SerializedMsg& out, HttpMsgCreator& packer, const Job& m) {
    json o;
    append_base(o, m);
    o[l_input] = m.input;
    o[l_difficulty] = m.difficulty;
    return append_json_msg(out, packer, o);
}

ResultCode parse_json_msg(const void* buf, size_t bufSize, Job& m) {
    json o;
    ResultCode r = parse_json(buf, bufSize, o);
    if (r != 0) return r;
    r = parse_message(o, m);
    if (r != 0) return r;
    m.input = o[l_input];
    m.difficulty = o[l_difficulty];
    return no_error;
}

bool append_json_msg(io::SerializedMsg& out, HttpMsgCreator& packer, const Cancel& m) {
    json o;
    append_base(o, m);
    return append_json_msg(out, packer, o);
}

ResultCode parse_json_msg(const void* buf, size_t bufSize, Cancel& m) {
    json o;
    ResultCode r = parse_json(buf, bufSize, o);
    if (r != 0) return r;
    r = parse_message(o, m);
    if (r != 0) return r;
    return no_error;
}

Solution::Solution(uint64_t _id, const Block::PoW& _pow) :
    Message(std::to_string(_id), solution)
{
    char buf[Block::PoW::nSolutionBytes * 2 + 1];
    nonce = to_hex(buf, _pow.m_Nonce.m_pData, Block::PoW::NonceType::nBytes);
    output = to_hex(buf, _pow.m_Indices.data(), Block::PoW::nSolutionBytes);
}

bool Solution::fill_pow(Block::PoW& pow) {
    bool ok = false;
    std::vector<uint8_t> buf = from_hex(output, &ok);
    if (!ok || buf.size() != Block::PoW::nSolutionBytes) return false;
    buf.clear();
    buf = from_hex(nonce, &ok);
    return (ok && buf.size() == Block::PoW::NonceType::nBytes);
}

bool append_json_msg(io::SerializedMsg& out, HttpMsgCreator& packer, const Solution& m) {
    json o;
    append_base(o, m);
    o[l_nonce] = m.nonce;
    o[l_output] = m.output;
    return append_json_msg(out, packer, o);
}

ResultCode parse_json_msg(const void* buf, size_t bufSize, Solution& m) {
    json o;
    ResultCode r = parse_json(buf, bufSize, o);
    if (r != 0) return r;
    r = parse_message(o, m);
    if (r != 0) return r;
    m.nonce = o[l_nonce];
    m.output = o[l_output];
    return no_error;
}

bool append_json_msg(io::SerializedMsg& out, HttpMsgCreator& packer, const Result& m) {
    json o;
    append_base(o, m);
    o[l_code] = m.code;
    o[l_description] = m.description;
    return append_json_msg(out, packer, o);
}

ResultCode parse_json_msg(const void* buf, size_t bufSize, Result& m) {
    json o;
    ResultCode r = parse_json(buf, bufSize, o);
    if (r != 0) return r;
    r = parse_message(o, m);
    if (r != 0) return r;
    m.code = o[l_code];
    m.description = o[l_description];
    return no_error;
}

} //namespaces
