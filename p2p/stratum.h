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
#include "utility/io/buffer.h"
#include <string>

namespace beam {

class HttpMsgCreator;

namespace stratum {

#define STRATUM_METHODS(macro) \
    macro(0, null_method, Dummy) \
    macro(1, login, Login) \
    macro(2, status, Status)

#define STRATUM_ERRORS(macro) \
    macro(0, no_error, "") \
    macro(-32000, message_corrupted, "Message corrupted") \
    macro(-32001, unknown_method, "Unknown method") \
    macro(-32002, empty_id, "ID is empty")

enum Method {
#define DEF_METHOD(_, M, __) M,
    STRATUM_METHODS(DEF_METHOD)
#undef DEF_METHOD
    METHODS_END
};

enum ErrorCode {
#define DEF_ERROR(code, error, _) error=code,
    STRATUM_ERRORS(DEF_ERROR)
#undef DEF_ERROR
    ERRORS_END
};

// returns empty string if methodId >= METHODS_END or 0
std::string get_method_str(Method methodId);

// returns 0 for all unknown methods
Method get_method(const std::string& str);

std::string get_error_msg(int code);

struct Message {
    Method method;
    std::string id;
    std::string method_str;

    virtual ~Message() = default;

    Message() : method(null_method) {}

    Message(uint64_t _id, Method _method) :
        method(_method),
        id(std::to_string(_id)),
        method_str(get_method_str(_method))
    {}
};

struct Error {
    int code;
    std::string error_msg;

    Error() : code(0) {}
    Error(int _code) :
        code(_code),
        error_msg(get_error_msg(code))
    {}
};

struct Response : Message {
    Error error;

    Response(uint64_t _id, Method _method, Error _error) :
        Message(_id, _method),
        error(std::move(_error))
    {}

    Response() = default;
};

struct DummyRequest {};
struct DummyResponse {};
struct LoginRequest : Message {};
struct LoginResponse : Response {};
struct StatusRequest : Message {};
struct StatusResponse : Response {};

struct ParserCallback {
    virtual ~ParserCallback() = default;

    virtual void on_error(ErrorCode code) {}
    virtual void on_unsupported_method(Method method, bool isRequest) { on_error(unknown_method); }

#define DEF_HANDLER(code, label, prefix) \
    virtual void on_message(const prefix ## Request & r) { on_unsupported_method(label, true); } \
    virtual void on_message(const prefix ## Response & r) { on_unsupported_method(label, false); }

    STRATUM_METHODS(DEF_HANDLER)

#undef DEF_HANDLER

};

// returns empty buffer if m fields contain unexpected chars (i.e. non-utf8)
template<typename M> io::SharedBuffer create_json_msg(HttpMsgCreator& packer, const M& m);

template<> io::SharedBuffer create_json_msg(HttpMsgCreator& packer, const Response& m);

// returns 0 or error code from STRATUM_ERRORS
template<typename M> int parse_json_msg(const void* buf, size_t bufSize, M& m);

template<> int parse_json_msg(const void* buf, size_t bufSize, Response& m);

}} //namespaces
