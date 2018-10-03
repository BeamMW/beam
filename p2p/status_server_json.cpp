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

#include "status_server_json.h"
#include "stratum.h"
#include "utility/nlohmann/json.hpp"

namespace beam {

io::SharedBuffer dump_to_json(HttpMsgCreator& packer, const DummyStatus& ds) {
    return stratum::dump(packer, json{ {"timestamp", ds.timestamp }, { "height", ds.height }} );
}

} //namespace

