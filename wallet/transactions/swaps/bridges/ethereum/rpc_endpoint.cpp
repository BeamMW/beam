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

#include "rpc_endpoint.h"
#include <algorithm>
#include <cctype>

namespace beam::ethereum
{
namespace
{
std::string trimOuter(const std::string& s)
{
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool hasForbiddenChars(const std::string& s)
{
    return std::any_of(s.begin(), s.end(), [](unsigned char c) {
        return c <= 0x20 || c == 0x7f; // whitespace + control chars
    });
}

std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

bool validHostChar(char c)
{
    return std::isalnum((unsigned char)c) || c == '.' || c == '-';
}

} // namespace

bool ParseEthereumRpcUrl(const std::string& rawUrl, RpcEndpoint& out)
{
    const std::string url = trimOuter(rawUrl);
    if (url.empty() || hasForbiddenChars(url))
        return false;

    // scheme
    auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos)
        return false;
    const std::string scheme = toLower(url.substr(0, schemeEnd));
    bool ssl;
    if (scheme == "https")      ssl = true;
    else if (scheme == "http")  ssl = false;
    else                        return false;

    std::string rest = url.substr(schemeEnd + 3);

    // split authority / path
    std::string::size_type pathPos;
    if (!rest.empty() && rest.front() == '[')
    {
        auto close = rest.find(']');
        auto slash = rest.find('/');
        if (close == std::string::npos || (slash != std::string::npos && slash < close))
            return false; // unterminated bracket, or '/' before closing bracket
        pathPos = rest.find('/', close);
    }
    else
    {
        pathPos = rest.find('/');
    }
    std::string authority = (pathPos == std::string::npos) ? rest : rest.substr(0, pathPos);
    std::string pathAndQuery = (pathPos == std::string::npos) ? "/" : rest.substr(pathPos);

    if (authority.empty() || authority.find('@') != std::string::npos) // no credentials
        return false;

    // host[:port]. Bracketed IPv6 literals are rejected: the HTTP transport
    // (io::Address::resolve) is IPv4-only and splits host:port on the first
    // colon, so an accepted IPv6 endpoint could never connect.
    std::string host;
    std::string portStr;
    if (authority.front() == '[')
        return false;

    auto colon = authority.find(':');
    host = authority.substr(0, colon == std::string::npos ? authority.size() : colon);
    if (colon != std::string::npos)
        portStr = authority.substr(colon + 1);
    if (host.empty() ||
        !std::all_of(host.begin(), host.end(), validHostChar))
        return false;

    uint16_t port = ssl ? 443 : 80;
    if (!portStr.empty())
    {
        if (portStr.size() > 5 ||
            !std::all_of(portStr.begin(), portStr.end(),
                         [](unsigned char c) { return std::isdigit(c); }))
            return false;
        unsigned long p = std::stoul(portStr);
        if (p < 1 || p > 65535)
            return false;
        port = static_cast<uint16_t>(p);
    }

    out.m_ssl = ssl;
    out.m_host = toLower(host);
    out.m_port = port;
    out.m_pathAndQuery = pathAndQuery; // case preserved: API keys are case-sensitive
    return true;
}

std::string SanitizeRpcUrlForLog(const std::string& url)
{
    RpcEndpoint ep;
    if (!ParseEthereumRpcUrl(url, ep))
        return "<invalid rpc url>";
    std::string res = (ep.m_ssl ? "https://" : "http://") + ep.m_host;
    if (ep.m_port != (ep.m_ssl ? 443 : 80))
        res += ":" + std::to_string(ep.m_port);
    return res;
}
} // namespace beam::ethereum
