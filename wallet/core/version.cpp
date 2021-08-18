// Copyright 2020 The Beam Team
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

#include "version.h"
#include "utility/string_helpers.h"
#include "common.h"
#include <regex>


namespace std
{
    string to_string(const beam::Version& v)
    {
        string maj(to_string(v.m_major));
        string min(to_string(v.m_minor));
        string rev(to_string(v.m_revision));
        string res;
        res.reserve(maj.size() + min.size() + rev.size());
        res.append(maj).push_back('.');
        res.append(min).push_back('.');
        res.append(rev);
        return res;
    }
}

namespace beam
{
    bool Version::from_string(const std::string& verString)
    {
        try
        {
            std::regex libVersionRegex("\\d{1,}\\.\\d{1,}\\.\\d{1,}");
            if (std::regex_match(verString, libVersionRegex))
            {
                auto stringList = string_helpers::split(verString, '.');
                if (stringList.size() > 3) return false;

                std::vector<uint32_t> verList(3, 0);
                auto it = std::begin(verList);

                for (const auto& str : stringList)
                {
                    size_t strEnd = 0;
                    uint32_t integer = std::stoul(str, &strEnd);
                    if (strEnd != str.size())
                    {
                        return false;
                    }
                    *it = integer;
                    it++;
                }
                m_major = verList[0];
                m_minor = verList[1];
                m_revision = verList[2];
            }
            else
            {
                return wallet::fromByteBuffer(ByteBuffer{ verString.begin(), verString.end() }, *this);
            }
        }
        catch(...)
        {
            return false;
        }
        return true;
    }

    bool Version::operator==(const Version& other) const
    {
        return m_major == other.m_major
            && m_minor == other.m_minor
            && m_revision == other.m_revision;
    }

    bool Version::operator!=(const Version& other) const
    {
        return !(*this == other);
    }

    bool Version::operator<(const Version& other) const
    {
        return m_major < other.m_major
            || (m_major == other.m_major
                && (m_minor < other.m_minor
                    || (m_minor == other.m_minor && m_revision < other.m_revision)));
    }

    bool Version::operator<=(const Version& other) const
    {
        return *this == other || *this < other;
    }

    bool ClientVersion::operator<(const ClientVersion& other) const
    {
        return m_revision < other.m_revision;
    }
} // namespace beam
