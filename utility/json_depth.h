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

#include "nlohmann/json.hpp"
#include <string>

namespace beam
{
    // nlohmann::json parses, dumps and destroys values recursively, without a depth limit, so an untrusted
    // deeply nested document like [[[[...]]]] overflows the stack. A level takes ~190 bytes in a release build,
    // so this limit is safe even for the 512 KB stacks of secondary threads, and is far beyond real documents.
    constexpr size_t MaxJsonDepth = 128;

    // Returns the position where the nesting gets deeper than maxDepth, or nullptr if it doesn't.
    // Counts brackets outside of strings the way the lexer sees them, which bounds the depth json::parse reaches.
    // O(n), no allocations.
    inline const char* FindJsonDepthExcess(const char* p, const char* end, size_t maxDepth = MaxJsonDepth)
    {
        size_t depth = 0;
        bool inString = false;

        for (; p != end; ++p)
        {
            const char c = *p;
            if (inString)
            {
                if (c == '\\')
                {
                    if (++p == end)
                        break;
                }
                else if (c == '"')
                    inString = false;
            }
            else if (c == '"')
                inString = true;
            else if ((c == '[') || (c == '{'))
            {
                if (++depth > maxDepth)
                    return p;
            }
            else if (((c == ']') || (c == '}')) && depth)
                --depth;
        }

        return nullptr;
    }

    // json::parse for untrusted input. A too deep document is rejected with json::parse_error, like any malformed one
    inline nlohmann::json ParseUntrustedJson(const char* p, const char* end)
    {
        if (const char* pExcess = FindJsonDepthExcess(p, end))
            throw nlohmann::detail::parse_error::create(101, static_cast<size_t>(pExcess - p) + 1,
                "nesting is deeper than " + std::to_string(MaxJsonDepth));

        return nlohmann::json::parse(p, end);
    }

    inline nlohmann::json ParseUntrustedJson(const std::string& s)
    {
        return ParseUntrustedJson(s.data(), s.data() + s.size());
    }
}
