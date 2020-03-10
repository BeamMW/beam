// Copyright 2019 The Beam Team
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

#include "../bitcoin/common.h"

namespace beam::denarius
{
    constexpr uint64_t kDustThreshold = bitcoin::kDustThreshold;
    extern const char kMainnetGenesisBlockHash[];
    extern const char kTestnetGenesisBlockHash[];
    extern const char kRegtestGenesisBlockHash[];

    uint8_t getAddressVersion();
    std::vector<std::string> getGenesisBlockHashes();
} // namespace beam::denarius
