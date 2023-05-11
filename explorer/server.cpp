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

    void (Server::*pFn)(const HttpConnection::Ptr&) = 0;

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

        _body.clear();

        //bool validKey = _acl.check(_currentUrl.args["m"], _currentUrl.args["n"], _currentUrl.args["h"]);
        bool validKey = _acl.check(conn->peer_address());
        if (!validKey)
            send(conn, 403, "Forbidden");
        else
        {
            try
            {
                (this->*pFn)(conn);
                keepalive = send(conn, 200, "OK");
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

#define OnRequest(dir) void Server::on_request_##dir(const HttpConnection::Ptr& conn)

OnRequest(status)
{
    if (!_backend.get_status(_body))
        Exc::Fail("#1");
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
    {
        if (!_backend.get_block_by_kernel(_body, hv))
            Exc::Fail("#2.1");
    }
    else 
    {
        auto height = _currentUrl.get_int_arg("height", 0);
        if (!_backend.get_block(_body, height))
            Exc::Fail("#2.2");
    }
}

OnRequest(blocks)
{
    auto start = _currentUrl.get_int_arg("height", 0);
    auto n = _currentUrl.get_int_arg("n", 0);
    if (start <= 0 || n < 0)
        Exc::Fail("#3.1");

    if (!_backend.get_blocks(_body, start, n))
        Exc::Fail("#3.2");
}

OnRequest(peers)
{
    if (!_backend.get_peers(_body))
        Exc::Fail("#4");
}

OnRequest(swap_offers)
{
    if (!_backend.get_swap_offers(_body))
        Exc::Fail("#5");
}

OnRequest(swap_totals)
{
    if (!_backend.get_swap_totals(_body))
        Exc::Fail("#6");
}

OnRequest(contracts)
{
    if (!_backend.get_contracts(_body))
        Exc::Fail("#7");
}

OnRequest(contract)
{

    ECC::Hash::Value id;
    if (!get_UrlHexArg(_currentUrl, "id", id))
        Exc::Fail("id missing");

    beam::Height hMin = _currentUrl.get_int_arg("hMin", 0);
    beam::Height hMax = _currentUrl.get_int_arg("hMax", -1);
    uint32_t nMaxTxs = (uint32_t) _currentUrl.get_int_arg("nMaxTxs", static_cast<uint32_t>(-1));

    if (!_backend.get_contract_details(_body, id, hMin, hMax, nMaxTxs))
        Exc::Fail("#8");
}

OnRequest(asset)
{

    auto aid = _currentUrl.get_int_arg("id", 0);
    beam::Height hMin = _currentUrl.get_int_arg("hMin", 0);
    beam::Height hMax = _currentUrl.get_int_arg("hMax", -1);
    uint32_t nMaxOps = (uint32_t) _currentUrl.get_int_arg("nMaxOps", -1);

    if (!_backend.get_asset_history(_body, (uint32_t) aid, hMin, hMax, nMaxOps))
        Exc::Fail("#9");
}

bool Server::send(const HttpConnection::Ptr& conn, int code, const char* message)
{
    assert(conn);

    size_t bodySize = 0;
    for (const auto& f : _body) { bodySize += f.size; }

    bool ok = _msgCreator.create_response(
        _headers,
        code,
        message,
        0, //headers,
        0, //sizeof(headers) / sizeof(HeaderPair),
        1,
        "application/json",
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
