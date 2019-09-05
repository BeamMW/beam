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

namespace beam::bitcoin
{
    constexpr uint32_t kTransactionVersion = 2;
    constexpr uint64_t kDustThreshold = 546;
    constexpr uint32_t kBTCWithdrawTxAverageSize = 360;

    uint8_t getAddressVersion();
} // namespace beam::bitcoin
