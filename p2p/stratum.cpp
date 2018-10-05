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
#include "http_msg_creator.h"
#include "utility/nlohmann/json.hpp"
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

std::string get_error_msg(int code) {
    if (code == 0) return std::string();
    switch (code) {
#define ERROR_MESSAGE(code, _, message) case code: return message;
        STRATUM_ERRORS(ERROR_MESSAGE)
#undef ERROR_MESSAGE
        default: break;
    }
    return "Unknown error";
}

namespace {

struct JsonOutputAdapter : nlohmann::detail::output_adapter_protocol<char> {
    JsonOutputAdapter(io::FragmentWriter& _fw) : fw(_fw) {}

    void write_character(char c) override {
        fw.write(&c, 1);
    }

    void write_characters(const char* s, std::size_t length) override {
        fw.write(s, length);
    }

    io::FragmentWriter& fw;
};

#define DEF_LABEL(label) static const std::string l_##label (#label)
    DEF_LABEL(jsonrpc);
    DEF_LABEL(id);
    DEF_LABEL(method);
    DEF_LABEL(error);
    DEF_LABEL(code);
    DEF_LABEL(message);
#undef DEF_LABEL

int parse_message(const json& o, Message& m) {
    m.id = o[l_id];
    if (m.id.empty()) return empty_id;
    m.method_str = o[l_method];
    m.method = get_method(m.method_str);
    if (m.method == 0) return unknown_method;
    return 0;
}

int parse_error(const json& o, Error& e) {
    const json& eo = o[l_error];
    if (eo.empty() || eo.type() == json::value_t::null) {
        e.code = 0;
        e.error_msg.clear();
        return 0;
    }
    e.code = eo[l_code].get<int>();
    e.error_msg = eo[l_message];
    return 0;
}

} //namespace

template<> io::SharedBuffer create_json_msg(HttpMsgCreator& packer, const Response& m) {
    json o;
    o[l_jsonrpc] = "2.0";
    o[l_id] = m.id;
    o[l_method] = m.method_str;
    if (m.error.code != 0) {
        json e;
        e[l_code] = m.error.code;
        e[l_message] = m.error.error_msg;
        o[l_error] = std::move(e);
    } else {
        o[l_error] = nullptr;
    }

    return dump(packer, o);
}

template<> int parse_json_msg(const void* buf, size_t bufSize, Response& m) {
    if (bufSize == 0) return false;
    const char* bufc = (const char*)buf;
    json o;
    try {
        o = json::parse(bufc, bufc + bufSize);
    } catch (const std::exception& e) {
        LOG_ERROR() << "json parse: " << e.what() << "\n" << std::string(bufc, bufc + (bufSize > 1024 ? 1024 : bufSize));
        return message_corrupted;
    }
    int err = parse_message(o, m);
    if (err != 0) return err;
    return parse_error(o, m.error);
}

io::SharedBuffer dump(HttpMsgCreator& packer, const json& o) {
    io::SharedBuffer result;
    try {
        // TODO make stateful object out of these fns if performance issues occur

        io::SerializedMsg sm;
        io::FragmentWriter& fw = packer.acquire_writer(sm);
        nlohmann::detail::serializer<json> s(std::make_shared<JsonOutputAdapter>(fw), ' ');
        s.dump(o, false, false, 0);
        fw.finalize();
        result = io::normalize(sm, false);

    } catch (const std::exception& e) {
        LOG_ERROR() << "dump json: " << e.what();
    }

    packer.release_writer();
    return result;
}

} //namespaces
