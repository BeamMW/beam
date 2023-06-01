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

#include "server.h"
#include "adapter.h"
#include "utility/logger.h"
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <fstream>
#include "../core/ecc.h"
#include "../core/block_crypt.h"

namespace beam { namespace explorer {

namespace {

#define STS "Explorer server: "

static const uint64_t SERVER_RESTART_TIMER = 1;
static const uint64_t ACL_REFRESH_TIMER = 2;
static const unsigned SERVER_RESTART_INTERVAL = 1000;
static const unsigned ACL_REFRESH_INTERVAL = 5555;

} //namespace

Server::Server(IAdapter& adapter, io::Reactor& reactor, io::Address bindAddress, const std::string& keysFileName, const std::vector<uint32_t>& whitelist) :
    _msgCreator(2000),
    _backend(adapter),
    _reactor(reactor),
    _timers(reactor, 100),
    _bindAddress(bindAddress),
    _acl(keysFileName), //TODO
    _whitelist(whitelist)
{
    _timers.set_timer(SERVER_RESTART_TIMER, 0, BIND_THIS_MEMFN(start_server));
    _timers.set_timer(ACL_REFRESH_TIMER, ACL_REFRESH_INTERVAL, BIND_THIS_MEMFN(refresh_acl));
}

void Server::start_server() {
    try {
        _server = io::TcpServer::create(
            _reactor,
            _bindAddress,
            BIND_THIS_MEMFN(on_stream_accepted)
        );
        LOG_INFO() << STS << "listens to " << _bindAddress;
    } catch (const std::exception& e) {
        LOG_ERROR() << STS << "cannot start server: " << e.what() << " restarting in  " << SERVER_RESTART_INTERVAL << " msec";
        _timers.set_timer(SERVER_RESTART_TIMER, SERVER_RESTART_INTERVAL, BIND_THIS_MEMFN(start_server));
    }
}

void Server::refresh_acl() {
    _acl.refresh();
    _timers.set_timer(ACL_REFRESH_TIMER, ACL_REFRESH_INTERVAL, BIND_THIS_MEMFN(refresh_acl));
}

void Server::on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode) {
    if (errorCode == 0) {

        auto peer = newStream->peer_address();

        if (!_whitelist.empty())
        {
            if (std::find(_whitelist.begin(), _whitelist.end(), peer.ip()) == _whitelist.end())
            {
                LOG_WARNING() << peer.str() << " not in IP whitelist, closing";
                return;
            }
        }

        newStream->enable_keepalive(1);
        LOG_DEBUG() << STS << "+peer " << peer;
        _connections[peer.u64()] = std::make_unique<HttpConnection>(
            peer.u64(),
            BaseConnection::inbound,
            BIND_THIS_MEMFN(on_request),
            10000,
            1024,
            std::move(newStream)
        );
    } else {
        LOG_ERROR() << STS << io::error_str(errorCode) << ", restarting server in  " << SERVER_RESTART_INTERVAL << " msec";
        _timers.set_timer(SERVER_RESTART_TIMER, SERVER_RESTART_INTERVAL, BIND_THIS_MEMFN(start_server));
    }
}





void json2Msg(const json& obj, io::SerializedMsg& out)
{

    io::SerializedMsg sm;
    io::SharedBuffer body;
    HttpMsgCreator packer(4096);

    if (!serialize_json_msg(sm, packer, obj))
        Exc::Fail("couldn't serialized");

    out.push_back(io::normalize(sm, false));
}

struct HtmlConverter
{
    std::ostringstream m_os;
    uint32_t m_Depth = 0;
    uint32_t m_Tbl = 0;
    const std::string& m_Url;

    HtmlConverter(const std::string& url) :m_Url(url) {}

    static std::string get_ShortOf(const std::string& s, uint32_t nMaxChars = 13)
    {
        static const char s_szSufix[] = "...";

        if (s.size() <= nMaxChars + _countof(s_szSufix) - 1)
            return s;

        std::string s2;
        s2.assign(s.begin(), s.begin() + nMaxChars);
        s2 += s_szSufix;
        return s2;
    }

    void OnTableData(const json& obj)
    {
        for (size_t i = 0; i < obj.size(); i++)
        {
            auto& x = obj.at(i);
            if (x.is_array())
            {
                m_os << "<tr>";

                for (size_t j = 0; j < x.size(); j++)
                {
                    m_os << "<td";
                    if ((m_Tbl > 1) && (j + 1 < x.size()))
                        m_os << " style = \"width:" << (100 / x.size()) << "%\"";
                    m_os << ">";
                    OnObjInternal(x.at(j));
                    m_os << "</td>";
                }

                m_os << "</tr>\n";
            }
            else
                OnObjInternal(x);

        }
    }

    bool OnTable(const json& obj)
    {
        if (!obj.is_array())
            return false;

        m_os << "<table style=\"width:100%\">\n";
        OnTableData(obj);
        m_os << "</table>\n";
        return true;
    }

    std::string Encode(std::string&& s)
    {
        std::string sRet;

        for (size_t i = 0; i < s.size(); i++)
        {
            const char* szSubst = nullptr;
            char ch = s[i];
            switch (ch)
            {
            case '<': szSubst = "&#60;"; break;
            case '>': szSubst = "&#62;"; break;
            case '&': szSubst = "&#38;"; break;
            }

            if (szSubst)
            {
                if (sRet.empty())
                    sRet.assign(s.c_str(), i);
                sRet += szSubst;
            }
            else
            {
                if (!sRet.empty())
                    sRet += ch;
            }
        }


        return sRet.empty() ? s : sRet;
    }

    bool OnObjSpecial(const json& obj)
    {
        auto itT = obj.find("type");
        auto itV = obj.find("value");
        if ((obj.end() == itT) || (obj.end() == itV))
            return false;

        auto& objT = *itT;
        auto& objV = *itV;
        if (json::value_t::string != objT.type())
            return false;

        const auto& sType = objT.get<std::string>();
        if (sType == "aid")
        {
            if (!objV.is_number())
                return false;

            auto aid = objV.get<Asset::ID>();
            if (aid)
                m_os << "<a href = \"asset?htm=1&id=" << aid << "\">Asset-" << aid << "</a>";
            else
                m_os << "Beam";

            return true;
        }

        if (sType == "amount")
        {
            uint64_t val = 0;
            AmountBig::Type valBig;
            bool bMinus = false;
            bool bBig = false;

            if (objV.is_number())
                val = objV.get<int64_t>();
            else
            {
                if (!objV.is_string())
                    return false;

                const auto& s = objV.get<std::string>();
                auto sz = s.c_str();
                if (*sz == '-')
                {
                    bMinus = true;
                    sz++;
                }

                // convert it. NOTE - this may be a BIG number, don't use std::stoll.
                bBig = true;
                ReadUIntBig(valBig, sz);
            }

            m_os << "<p2 style=\"color:" << (bMinus ? "blue" : "green") << "\">";
            if (bMinus)
                m_os << "-";

            if (bBig)
                AmountBig::Print(m_os, valBig, false);
            else
                AmountBig::Print(m_os, val, false);

            m_os << "</p2>";

            return true;
        }

        if (sType == "cid")
        {
            if (!objV.is_string())
                return false;

            auto sCid = Encode(objV.get<std::string>());

            m_os << "<a href = \"contract?htm=1&id=" << sCid << "\">cid-" << get_ShortOf(sCid) << "</a>";

            return true;
        }

        if (sType == "th")
        {
            if (!objV.is_string())
                return false;

            m_os << "<h3 align=center>" << Encode(objV.get<std::string>()) << "</h3>";
            return true;
        }

        if (sType == "group")
        {
            // part of the table
            if (!objV.is_array())
                return false;

            m_os << "<tr></tr><tr></tr><tr></tr>";
            OnTableData(objV);
            m_os << "<tr></tr><tr></tr><tr></tr>";
            return true;

        }

        if (sType == "table")
        {
            HtmlConverter cvt2(m_Url);
            cvt2.m_Depth = m_Depth;
            cvt2.m_Tbl = m_Tbl + 1;

            if (!cvt2.OnTable(objV))
                return false;

            m_os << cvt2.m_os.str();

            auto itMore = obj.find("more");
            if (obj.end() != itMore)
            {
                auto& jMore = *itMore;
                std::string sPath = m_Url;

                for (auto itArg = jMore.begin(); jMore.end() != itArg; itArg++)
                {
                    auto& vArg = *itArg;
                    std::string sArg;
                    if (vArg.is_string())
                        sArg = Encode(vArg.get<std::string>());
                    else
                    {
                        if (vArg.is_number())
                            sArg = std::to_string(vArg.get<uint64_t>());
                    }

                    sPath = SubstituteArg(sPath, itArg.key(), sArg);
                }

                m_os << "<a href = \"" << sPath << "\">More..." << "</a>";
            }


            return true;
        }


        return false;
    }

    static void ReadUIntBig(AmountBig::Type& res, const char* sz)
    {
        res = Zero;

        for (bool bFirstTime = true; ; )
        {
            uint32_t nAdd = 0;
            uint32_t nMul = 1;

            const uint32_t maxChars = 9u;
            uint32_t iChar = 0;

            for (; iChar < maxChars; iChar++)
            {
                uint8_t val = sz[iChar] - '0';
                if (val > 9)
                    break;

                nAdd = nAdd * 10u + val;
                nMul *= 10u;
            }


            if (!iChar)
                break;

            if (bFirstTime)
                bFirstTime = false;
            else
                res = res * uintBigFrom(nMul);

            res += uintBigFrom(nAdd);

            if (iChar < maxChars)
                break;
            sz += maxChars;
        }

    }

    static std::string SubstituteArg(const std::string& sUrl, const std::string& sKey_, const std::string& sVal)
    {
        std::string sKey = sKey_ + '=';
        std::string sRes;
        sRes.reserve(sUrl.size() + sKey.size() + sVal.size());

        uint32_t iArg = 0, nArgs = 0;
        bool bHasSepAtEnd = false;

        for (size_t i = 0; i < sUrl.size(); iArg++)
        {
            size_t iNext = sUrl.find(iArg ? '&' : '?', i);
            bool bEnd = (std::string::npos == iNext);
            if (bEnd)
                iNext = sUrl.size();
            else
                iNext++;

            if (strncmp(sUrl.c_str() + i, sKey.c_str(), sKey.size()))
            {
                sRes.append(sUrl.c_str() + i, iNext - i);
                nArgs++;
                bHasSepAtEnd = !bEnd;
            }

            i = iNext;
        }

        if (!bHasSepAtEnd)
            sRes += (nArgs ? '&' : '?');
        sRes += sKey;
        sRes += sVal;
        return sRes;
    }


    void OnObjInternal(const json& obj)
    {
        uint32_t nDepth = m_Depth;
        if (++nDepth > 128)
            Exc::Fail("recursion too deep");

        TemporarySwap ts(m_Depth, nDepth);

        switch (obj.type())
        {
        case json::value_t::object:
        {
            if (OnObjSpecial(obj))
                break;

            m_os << "<ul>";

            for (auto it = obj.begin(); obj.end() != it; it++)
            {

                m_os << "<li>" << it.key() << ": ";
                OnObjInternal(*it);
                m_os << "</li>";
            }

            m_os << "</ul>";
        }
        break;

        case json::value_t::array:
        {
            m_os << "[";
            size_t n = obj.size();
            for (size_t i = 0; i < n; i++)
            {
                if (i)
                    m_os << ", ";
                OnObjInternal(obj.at(i));
            }
            m_os << "]";
        }
        break;

        case json::value_t::string:
            m_os << Encode(obj.get<std::string>());
            break;

        case json::value_t::number_integer:
            m_os << obj.get<int64_t>();
            break;

        case json::value_t::number_unsigned:
            m_os << obj.get<uint64_t>();
            break;

        case json::value_t::number_float:
            m_os << obj.get<double>();
            break;

        case json::value_t::boolean:
        {
            auto val = obj.get<bool>();
            m_os << (val ? "true" : "false");
        }
        break;

        default:
            //case json::value_t::null:
            return;
        }
    }


    void Convert(const json& obj)
    {
        m_os << "\
<!DOCTYPE html>\n\
<html>\n\
<head>\n\
<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n\
<style>\n\
table, th, td {\n\
  border: 1px solid black;\n\
  border-collapse: collapse;\n\
}\n\
td {\n\
  text-align: right;\n\
}\n\
</style>\n\
</head>\n\
<body>\n";


        OnObjInternal(obj);

        m_os << "\
</body>\n\
</html>\n";
    }

    void get_Res(io::SerializedMsg& out)
    {
        auto sRes = m_os.str();
        m_os.clear();
        auto& x = out.emplace_back();
        x.assign(sRes.c_str(), sRes.size());
    }
};


void jsonExp(json& obj, uint32_t nDepth)
{
    if (++nDepth > 128)
        Exc::Fail("recursion too deep");

    if (obj.is_array())
    {
        for (size_t j = 0; j < obj.size(); j++)
            jsonExp(obj.at(j), nDepth);
    }
    else
    {
        if (!obj.is_object())
            return;

        for (auto it = obj.begin(); obj.end() != it; it++)
            jsonExp(*it, nDepth);

        auto itT = obj.find("type");
        auto itV = obj.find("value");
        if ((obj.end() == itT) || (obj.end() == itV))
            return;

        auto& objT = *itT;
        auto& objV = *itV;
        if (json::value_t::string != objT.type())
            return;

        const auto& sType = objT.get<std::string>();
        if (sType == "amount")
        {
            uint64_t val = 0;
            AmountBig::Type valBig;
            bool bMinus = false;
            bool bBig = false;

            if (objV.is_number())
                val = objV.get<int64_t>();
            else
            {
                if (!objV.is_string())
                    return;

                const auto& s = objV.get<std::string>();
                auto sz = s.c_str();
                if (*sz == '-')
                {
                    bMinus = true;
                    sz++;
                }

                // convert it. NOTE - this may be a BIG number, don't use std::stoll.
                bBig = true;
                HtmlConverter::ReadUIntBig(valBig, sz);
            }

            std::ostringstream os;
            if (bMinus)
                os << '-';

            if (bBig)
                AmountBig::Print(os, valBig, false);
            else
                AmountBig::Print(os, val, false);

            objV = os.str();

        }


    }
}


bool Server::on_request(uint64_t id, const HttpMsgReader::Message& msg)
{
    auto it = _connections.find(id);
    if (it == _connections.end()) return false;

    if (msg.what != HttpMsgReader::http_message || !msg.msg) {
        LOG_DEBUG() << STS << "-peer " << io::Address::from_u64(id) << " : " << msg.error_str();
        _connections.erase(id);
        return false;
    }

    enum struct DirType
    {
        Unused,
#define THE_MACRO(dir) dir,
        ExplorerNodeDirs(THE_MACRO)
#undef THE_MACRO
    };

    if (m_Dirs.empty())
    {
#define THE_MACRO(dir) m_Dirs[#dir] = (int) DirType::dir;
        ExplorerNodeDirs(THE_MACRO)
#undef THE_MACRO
    }

    const std::string& path = msg.msg->get_path();
    const HttpConnection::Ptr& conn = it->second;

    json (Server::*pFn)(const HttpConnection::Ptr&) = 0;

    if (_currentUrl.parse(path, m_Dirs))
    {
        switch ((DirType) _currentUrl.dir)
        {
#define THE_MACRO(dir) case DirType::dir: pFn = &Server::on_request_##dir; break;
            ExplorerNodeDirs(THE_MACRO)
#undef THE_MACRO

        default: // suppress warning
            break;
        }
    }

    bool keepalive = false;

    if (pFn)
    {
        if (_currentUrl.args.end() != _currentUrl.args.find("htm"))
            _backend.m_Mode = IAdapter::Mode::AutoHtml;
        else
        {
            if (_currentUrl.args.end() != _currentUrl.args.find("exp_am"))
                _backend.m_Mode = IAdapter::Mode::ExplicitType;
            else
                _backend.m_Mode = IAdapter::Mode::Legacy;
        }

        _body.clear();

        //bool validKey = _acl.check(_currentUrl.args["m"], _currentUrl.args["n"], _currentUrl.args["h"]);
        bool validKey = _acl.check(conn->peer_address());
        if (!validKey)
            send(conn, 403, "Forbidden");
        else
        {
            try
            {
                json j = (this->*pFn)(conn);

                io::SerializedMsg sm;

                switch (_backend.m_Mode)
                {
                case IAdapter::Mode::AutoHtml:
                    {
                        HtmlConverter cvt(path);
                        cvt.Convert(j);
                        cvt.get_Res(_body);
                    }
                    break;

                case IAdapter::Mode::ExplicitType:
                    jsonExp(j, 0);
                    // no break;

                default:
                    json2Msg(j, _body);
                }

                keepalive = send(conn, 200, "OK", IAdapter::Mode::AutoHtml == _backend.m_Mode);
            }
            catch (const std::exception& e)
            {
                std::ostringstream os;
                os << "Internal error: " << e.what();
                send(conn, 500, os.str().c_str());
            }
        }

    }
    else
        send(conn, 404, "Not Found");

    if (!keepalive)
    {
        conn->shutdown();
        _connections.erase(it);
    }
    return keepalive;
}

#define OnRequest(dir) json Server::on_request_##dir(const HttpConnection::Ptr& conn)

OnRequest(status)
{
    return _backend.get_status();
}

bool get_UrlHexArg(const HttpUrl& url, const std::string_view& name, uint8_t* p, uint32_t n)
{
    auto it = url.args.find(name);
    if (url.args.end() == it)
        return false;

    n <<= 1; // to txt len

    const auto& val = it->second;
    if (val.size() != n)
        return false;

    return uintBigImpl::_Scan(p, val.data(), n) == n;
}

template <uint32_t nBytes>
bool get_UrlHexArg(const HttpUrl& url, const std::string_view& name, uintBig_t<nBytes>& val)
{
    return get_UrlHexArg(url, name, val.m_pData, nBytes);
}

OnRequest(block)
{

    ECC::Hash::Value hv;
    if (get_UrlHexArg(_currentUrl, "kernel", hv))
        return _backend.get_block_by_kernel(hv);

    auto height = _currentUrl.get_int_arg("height", 0);
    return _backend.get_block(height);
}

OnRequest(blocks)
{
    auto start = _currentUrl.get_int_arg("height", 0);
    auto n = _currentUrl.get_int_arg("n", 0);
    if (start <= 0 || n < 0)
        Exc::Fail("#3.1");

    return _backend.get_blocks(start, n);
}

OnRequest(hdrs)
{
    Height hTop = _currentUrl.get_int_arg("hMax", std::numeric_limits<int64_t>::max());
    uint32_t n = (uint32_t) _currentUrl.get_int_arg("nMax", static_cast<uint32_t>(-1));

    return _backend.get_hdrs(hTop, n);
}

OnRequest(peers)
{
    return _backend.get_peers();
}

OnRequest(swap_offers)
{
    return _backend.get_swap_offers();
}

OnRequest(swap_totals)
{
    return _backend.get_swap_totals();
}

OnRequest(contracts)
{
    return _backend.get_contracts();
}

OnRequest(contract)
{

    ECC::Hash::Value id;
    if (!get_UrlHexArg(_currentUrl, "id", id))
        Exc::Fail("id missing");

    beam::Height hMin = _currentUrl.get_int_arg("hMin", 0);
    beam::Height hMax = _currentUrl.get_int_arg("hMax", -1);
    uint32_t nMaxTxs = (uint32_t) _currentUrl.get_int_arg("nMaxTxs", static_cast<uint32_t>(-1));

    return _backend.get_contract_details(id, hMin, hMax, nMaxTxs);
}

OnRequest(asset)
{

    auto aid = _currentUrl.get_int_arg("id", 0);
    beam::Height hMin = _currentUrl.get_int_arg("hMin", 0);
    beam::Height hMax = _currentUrl.get_int_arg("hMax", -1);
    uint32_t nMaxOps = (uint32_t) _currentUrl.get_int_arg("nMaxOps", -1);

    return _backend.get_asset_history((uint32_t) aid, hMin, hMax, nMaxOps);
}

bool Server::send(const HttpConnection::Ptr& conn, int code, const char* message, bool isHtml)
{
    assert(conn);

    size_t bodySize = 0;
    for (const auto& f : _body) { bodySize += f.size; }

    HeaderPair pHp[2];
    ZeroObject(pHp);
    pHp[0].head = "Access-Control-Allow-Origin";
    pHp[0].content_str = "*";
    pHp[1].head = "Access-Control-Allow-Headers";
    pHp[1].content_str = "*";

    bool ok = _msgCreator.create_response(
        _headers,
        code,
        message,
        pHp, //headers,
        _countof(pHp), //sizeof(headers) / sizeof(HeaderPair),
        1,
        isHtml ? "text/html" : "application/json",
        bodySize
    );

    if (ok) {
        auto result = conn->write_msg(_headers);
        if (result && bodySize > 0) {
            result = conn->write_msg(_body);
        }
        if (!result) ok = false;
    } else {
        LOG_ERROR() << STS << "cannot create response";
    }

    _headers.clear();
    _body.clear();
    return (ok && code == 200);
}

Server::IPAccessControl::IPAccessControl(const std::string &ipsFileName) :
    _enabled(!ipsFileName.empty()),
    _ipsFileName(ipsFileName),
    _lastModified(0)
{
    refresh();
}

void Server::IPAccessControl::refresh() {
    using namespace boost::filesystem;

    if (!_enabled) return;

    try {
        path p(_ipsFileName);
        auto t = last_write_time(p);
        if (t <= _lastModified) {
            return;
        }
        _lastModified = t;
        std::ifstream file(_ipsFileName);
        std::string line;
        while (std::getline(file, line)) {
            boost::algorithm::trim(line);
            if (line.size() < 7) continue;
            io::Address a;
            if (a.resolve(line.c_str()))
                _ips.insert(a.ip());
        }
    } catch (std::exception &e) {
        LOG_ERROR() << e.what();
    }
}

bool Server::IPAccessControl::check(io::Address peerAddress) {
    static const uint32_t localhostIP = io::Address::localhost().ip();
    if (!_enabled || peerAddress.ip() == localhostIP) return true;
    return _ips.count(peerAddress.ip()) > 0;
}

}} //namespaces
