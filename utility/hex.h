// Copyright 2018-2020 The Beam Team
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
#include <string>
#include <vector>
#include <cstdint>

namespace beam
{
    // Converts bytes to base16 string, writes to dst buffer.
    // dst must contain at least size*2 bytes + 1
    char* to_hex(char* dst, const void* bytes, size_t size);

    // Converts bytes to base16 string.
    std::string to_hex(const void* bytes, size_t size);

    // Converts hexdec string to vector of bytes, if wholeStringIsNumber!=0 it will contain true if the whole string is base16
    std::vector<uint8_t> from_hex(const std::string& str, bool* wholeStringIsNumber=nullptr);

} //namespace

