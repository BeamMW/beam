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

#include "http_json_serializer.h"
#include "http_msg_creator.h"
#include "nlohmann/json.hpp"
#include "utility/logger.h"
#include "utility/io/json_serializer.h"

namespace beam {

using json = nlohmann::json;

bool serialize_json_msg(io::SerializedMsg& out, HttpMsgCreator& packer, const json& o) {
    size_t initialFragments = out.size();
    io::FragmentWriter& fw = packer.acquire_writer(out);
    bool result = serialize_json_msg(fw, o);
    packer.release_writer();
    if (!result) out.resize(initialFragments);
    return result;
}

} //namespace


