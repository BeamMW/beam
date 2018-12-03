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
#include "utility/io/fragment_writer.h"
#include "nlohmann/json_fwd.hpp"

namespace beam {

struct HeaderPair {
    const char* head;
    union {
        const char* content_str;
        unsigned long content_num;
    };
    bool is_number;

    HeaderPair(const char* h="", const char* c="") :
        head(h), content_str(c), is_number(false)
    {}

    HeaderPair(const char* h, unsigned long n) :
        head(h), content_num(n), is_number(true)
    {}
};

class HttpMsgCreator {
public:
    explicit HttpMsgCreator(size_t fragmentSize) :
        _fragmentWriter(
            fragmentSize,
            16,
            [this](io::SharedBuffer&& fragment) {
                if (_currentMsg) _currentMsg->push_back(std::move(fragment));
            }
        ),
        _currentMsg(0)
    {}

    bool create_request(
        io::SerializedMsg& out,
        const char* method,
        const char* path,
        const HeaderPair* headers, size_t num_headers,
        int http_minor_version = 1,
        const char* content_type = "",
        size_t bodySize = 0
    );

    bool create_response(
        io::SerializedMsg& out,
        int code,
        const char* message,
        const HeaderPair* headers, size_t num_headers,
        int http_minor_version = 1,
        const char* content_type = 0,
        size_t bodySize = 0
    );

    // for external body serializations (json etc)
    io::FragmentWriter& acquire_writer(io::SerializedMsg& out) {
        _currentMsg = &out;
        return _fragmentWriter;
    }

    void release_writer() {
        _currentMsg = 0;
    }

private:
    io::FragmentWriter _fragmentWriter;
    io::SerializedMsg* _currentMsg;
};

// appends json msg to out by http packer
bool serialize_json_msg(io::SerializedMsg& out, HttpMsgCreator& packer, const nlohmann::json& o);

} //namespace
