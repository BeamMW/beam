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

#pragma once
#include <cstdint>
#include <string>

namespace beam::ethereum
{
struct RpcEndpoint
{
    bool        m_ssl = false;
    std::string m_host;          // no scheme, no port
    uint16_t    m_port = 0;      // always set (defaulted from scheme)
    std::string m_pathAndQuery;  // leading '/', "/" if absent
};

// Parses an "http://" or "https://" URL: host is a bare hostname/IPv4 or a
// bracketed IPv6 literal, an optional port must be in [1, 65535], and
// embedded credentials (user:pass@) are rejected. Returns false (and leaves
// 'out' untouched) on any violation.
bool ParseEthereumRpcUrl(const std::string& url, RpcEndpoint& out);

// "https://host[:port]" — safe for logs (path may contain an API key).
std::string SanitizeRpcUrlForLog(const std::string& url);
}
