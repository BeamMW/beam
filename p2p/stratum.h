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
#include <stdint.h>
#include <string>

namespace beam::stratum {

enum Method {
    null_method,
    login,
    status,
    // etc
    METHODS_END
};

// throws if methodId >= METHODS_END
std::string get_method_str(Method methodId);

std::string get_error_msg(int code);

struct Message {
    uint64_t id;
    Method method;
    std::string id_str;
    std::string method_str;

    virtual ~Message() = default;

    Message() : id(0), method(null_method) {}

    Message(uint64_t _id, Method _method) :
        id(_id),
        method(_method),
        id_str(std::to_string(id)),
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
};

} //namespaces
