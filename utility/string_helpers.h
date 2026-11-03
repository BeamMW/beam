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

#include <string>
#include <vector>

namespace string_helpers
{
    std::vector<std::string> split(const std::string& s, char delim, bool trimSpaces = true);
    std::string trimCommas(const std::string& s);

    // Replaces every byte that is not part of a well-formed UTF-8 sequence with U+FFFD, so the result
    // can be serialized as JSON (nlohmann::json::dump throws on invalid UTF-8). Use on strings from peers or the chain.
    // Works in place: a valid string is returned as is, without a copy
    std::string replaceInvalidUtf8(std::string&& s);
}
