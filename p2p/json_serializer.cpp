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

#include "json_serializer.h"
#include "http_msg_creator.h"
#include "nlohmann/json.hpp"
#include "utility/logger.h"

namespace beam {

using json = nlohmann::json;

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

} //namespace

bool serialize_json_msg(io::FragmentWriter& packer, const nlohmann::json& o) {
    bool result = true;
    try {
        // TODO make stateful object out of these fns if performance issues occur
        nlohmann::detail::serializer<json> s(std::make_shared<JsonOutputAdapter>(packer), ' ');
        s.dump(o, false, false, 0);
        // for stratum
        static const char eol = 10;
        packer.write(&eol, 1);
    } catch (const std::exception& e) {
        LOG_ERROR() << "dump json: " << e.what();
        result = false;
    }
    packer.finalize();
    return result;
}

bool serialize_json_msg(io::SerializedMsg& out, HttpMsgCreator& packer, const json& o) {
    size_t initialFragments = out.size();
    io::FragmentWriter& fw = packer.acquire_writer(out);
    bool result = serialize_json_msg(fw, o);
    packer.release_writer();
    if (!result) out.resize(initialFragments);
    return result;
}

} //namespace


