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
#include "utility/io/json_serializer.h"
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
    DEF_LABEL(height);
    DEF_LABEL(nonceprefix);
    DEF_LABEL(forkheight);
    DEF_LABEL(blockhash);
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

void append_base(json& o, const Message& m) {
    o[l_jsonrpc] = "2.0";
    o[l_id] = m.id;
    o[l_method] = m.method_str;
}

ResultCode parse_base(const json& o, Message& m) {
    try {
        m.id = o[l_id];
        if (m.id.empty()) return empty_id;
        m.method_str = o[l_method];
        m.method = get_method(m.method_str);
        if (m.method == 0) return unknown_method;
    } catch (const std::exception& e) {
        LOG_ERROR() << "json parse: " << e.what();
        return message_corrupted;
    }
    return no_error;
}

template <typename M> void parse(const json& o, M& m)
{}

template<> void parse(const json& o, Login& m) {
    m.api_key = o[l_api_key];
}

template<> void parse(const json& o, Job& m) {
    m.input = o[l_input];
    m.difficulty = o[l_difficulty];
    m.height = o[l_height];
}

template<> void parse(const json& o, Solution& m) {
    m.nonce = o[l_nonce];
    m.output = o[l_output];
}

template<> void parse(const json& o, Result& m) {
    m.code = o[l_code];
    m.description = o[l_description];
    m.nonceprefix = o.value(l_nonceprefix, std::string());
}

} //namespace

bool append_json_msg(io::FragmentWriter& packer, const Login& m) {
    json o;
    append_base(o, m);
    o[l_api_key] = m.api_key;
    return serialize_json_msg(packer, o);
}

template <typename M> ResultCode parse_json_msg(const void* buf, size_t bufSize, M& m) {
    json o;
    ResultCode r = parse_json(buf, bufSize, o);
    if (r != 0) return r;
    r = parse_base(o, m);
    if (r != 0) return r;
    parse(m);
    return no_error;
}

Job::Job(const std::string& _id, const Merkle::Hash& _input, const Block::PoW& _pow, Height height) :
    Message(_id, job),
    difficulty(_pow.m_Difficulty.m_Packed),
    height(height)
{
    char buf[72];
    input = to_hex(buf, _input.m_pData, _input.nBytes);
}

bool append_json_msg(io::FragmentWriter& packer, const Job& m) {
    json o;
    append_base(o, m);
    o[l_input] = m.input;
    o[l_difficulty] = m.difficulty;
    o[l_height] = m.height;
    return serialize_json_msg(packer, o);
}

bool append_json_msg(io::FragmentWriter& packer, const Cancel& m) {
    json o;
    append_base(o, m);
    return serialize_json_msg(packer, o);
}

Solution::Solution(const std::string& _id, const Block::PoW& _pow) :
    Message(_id, solution)
{
    char buf[Block::PoW::nSolutionBytes * 2 + 1];
    nonce = to_hex(buf, _pow.m_Nonce.m_pData, Block::PoW::NonceType::nBytes);
    output = to_hex(buf, _pow.m_Indices.data(), Block::PoW::nSolutionBytes);
}

bool Solution::fill_pow(Block::PoW& pow) const {
    bool ok = false;
    std::vector<uint8_t> buf = from_hex(output, &ok);
    if (!ok || buf.size() != Block::PoW::nSolutionBytes) return false;
    memcpy(pow.m_Indices.data(), buf.data(), Block::PoW::nSolutionBytes);
    buf.clear();
    buf = from_hex(nonce, &ok);
    if (!ok || buf.size() != Block::PoW::NonceType::nBytes) return false;
    memcpy(pow.m_Nonce.m_pData, buf.data(), Block::PoW::NonceType::nBytes);
    return true;
}

bool append_json_msg(io::FragmentWriter& packer, const Solution& m) {
    json o;
    append_base(o, m);
    o[l_nonce] = m.nonce;
    o[l_output] = m.output;
    return serialize_json_msg(packer, o);
}

bool append_json_msg(io::FragmentWriter& packer, const Result& m) {
    json o;
    append_base(o, m);
    o[l_code] = m.code;
    o[l_description] = m.description;
    if (!m.nonceprefix.empty()) o[l_nonceprefix] = m.nonceprefix;
    if (m.forkheight != MaxHeight) o[l_forkheight] = m.forkheight;
    if (!m.blockhash.empty()) o[l_blockhash] = m.blockhash;
    return serialize_json_msg(packer, o);
}

#define DEF_PARSE_IMPL(_, __, struct_name) \
    ResultCode parse_json_msg(const void* buf, size_t bufSize, struct_name& m) { \
        json o; \
        ResultCode r = parse_json(buf, bufSize, o); \
        if (r != 0) return r; \
        r = parse_base(o, m); \
        if (r != 0) return r; \
        parse(o, m); \
        return no_error; }

STRATUM_METHODS(DEF_PARSE_IMPL)

#undef DEF_PARSE_IMPL

namespace {

template <typename M> bool parse(const json& o, const Message& base, ParserCallback& callback) {
    M m;
    (Message&)m = base;
    parse<M>(o, m);
    return callback.on_message(m);
}

} //namespace

bool parse_json_msg(const void* buf, size_t bufSize, ParserCallback& callback) {
    json o;
    ResultCode r = parse_json(buf, bufSize, o);
    if (r != 0) return false;
    Message m;
    r = parse_base(o, m);
    if (r != 0) return false;

    try {
        switch (m.method) {
#define DEF_PARSE_IMPL(_, method_name, struct_name) case method_name: { return parse<struct_name>(o, m, callback); }
        STRATUM_METHODS(DEF_PARSE_IMPL)
#undef DEF_PARSE_IMPL
        default:break;
        }
    } catch (const std::exception& e) {
        LOG_ERROR() << "json parse: " << e.what();
    }
    return false;
}

} //namespaces
