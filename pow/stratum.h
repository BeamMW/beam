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
#include "core/block_crypt.h"
#include "utility/io/fragment_writer.h"
#include <string>

namespace beam::stratum {

#define STRATUM_METHODS(macro) \
    macro(0, null_method, Dummy) \
    macro(1, login, Login) \
    macro(2, job, Job) \
    macro(3, solution, Solution) \
    macro(4, result, Result) \
    macro(5, cancel, Cancel)

#define STRATUM_RESULTS(macro) \
    macro(0, no_error, "Success") \
    macro(1, solution_accepted, "accepted") \
    macro(2, solution_rejected, "rejected") \
    macro(3, solution_expired, "expired") \
    macro(-32000, message_corrupted, "Message corrupted") \
    macro(-32001, unknown_method, "Unknown method") \
    macro(-32002, empty_id, "ID is empty") \
    macro(-32003, login_failed, "Login failed")

enum Method {
#define DEF_METHOD(_, M, __) M,
    STRATUM_METHODS(DEF_METHOD)
#undef DEF_METHOD
    METHODS_END
};

enum ResultCode {
#define DEF_RESULT(C, R, _) R=C,
    STRATUM_RESULTS(DEF_RESULT)
#undef DEF_RESULT
    RESULTS_END
};

// returns empty string if methodId >= METHODS_END or 0
std::string get_method_str(Method methodId);

// returns 0 for all unknown methods
Method get_method(const std::string& str);

std::string get_result_msg(int code);

/// Message base
struct Message {
    Method method;
    std::string id;
    std::string method_str;

    virtual ~Message() = default;

    Message() : method(null_method) {}

    Message(std::string _id, Method _method) :
        method(_method),
        id(std::move(_id)),
        method_str(get_method_str(_method))
    {}
};

struct Dummy : Message {};

/// Miner logins to server
struct Login : Message {
    std::string api_key;

    Login() = default;

    explicit Login(std::string _api_key) : Message("login", login), api_key(std::move(_api_key))
    {}
};

/// Server posts a job
struct Job : Message {
    std::string input;
    uint32_t difficulty=0;
    Height height=0;

    Job() = default;

    Job(const std::string& _id, const Merkle::Hash& _input, const Block::PoW& _pow, Height height);
};

/// Servers cancel job with given id
struct Cancel : Message {
    Cancel() = default;

    explicit Cancel(uint64_t _id) : Message(std::to_string(_id), cancel)
    {}
};

/// Miner posts a solution
struct Solution : Message {
    std::string nonce;
    std::string output;

    Solution() = default;

    Solution(const std::string& _id, const Block::PoW& _pow);

    // fills only indices and nonce fields of pow
    bool fill_pow(Block::PoW& pow) const;
};

struct Result : Message {
    ResultCode code=no_error;
    std::string description;
    std::string nonceprefix;
    std::string blockhash;
    uint64_t forkheight = MaxHeight;

    Result() = default;

    Result(std::string _id, ResultCode _code) :
        Message(std::move(_id), result),
        code(_code),
        description(get_result_msg(_code))
    {}
};

struct ParserCallback {
    virtual ~ParserCallback() = default;

    virtual bool on_stratum_error(ResultCode code) { return true; }
    virtual bool on_unsupported_stratum_method(Method method) { return on_stratum_error(unknown_method); }

#define DEF_HANDLER(_, label, struct_name) \
    virtual bool on_message(const struct_name & r) { return on_unsupported_stratum_method(label); }

    STRATUM_METHODS(DEF_HANDLER)

#undef DEF_HANDLER
};

// common case of parse message
bool parse_json_msg(const void* buf, size_t bufSize, ParserCallback& callback);

// serializers and deserializers that return negative ResultCode on parse error
#define DEF_SERIALIZE_JSON(_, __, struct_name) \
    bool append_json_msg(io::FragmentWriter& packer, const struct_name& m); \
    ResultCode parse_json_msg(const void* buf, size_t bufSize, struct_name& m);

STRATUM_METHODS(DEF_SERIALIZE_JSON)

#undef DEF_SERIALIZE_JSON

} //namespace
