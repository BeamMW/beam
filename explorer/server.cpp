#include "server.h"
#include "adapter.h"
#include "utility/logger.h"
#include "secp256k1-zkp/src/hash_impl.h"
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <fstream>

namespace beam { namespace explorer {

namespace {

#define STS "Status server: "

static const uint64_t SERVER_RESTART_TIMER = 1;
static const uint64_t ACL_REFRESH_TIMER = 2;
static const unsigned SERVER_RESTART_INTERVAL = 1000;
static const unsigned ACL_REFRESH_INTERVAL = 5555;

enum Dirs {
    DIR_STATUS, DIR_BLOCK, DIR_BLOCKS
    // etc
};

} //namespace

Server::Server(IAdapter& adapter, io::Reactor& reactor, io::Address bindAddress, const std::string& keysFileName) :
    _msgCreator(2000),
    _backend(adapter),
    _reactor(reactor),
    _timers(reactor, 100),
    _bindAddress(bindAddress),
    _acl(keysFileName) //TODO
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

bool Server::on_request(uint64_t id, const HttpMsgReader::Message& msg) {
    auto it = _connections.find(id);
    if (it == _connections.end()) return false;

    if (msg.what != HttpMsgReader::http_message || !msg.msg) {
        LOG_DEBUG() << STS << "-peer " << io::Address::from_u64(id) << " : " << msg.error_str();
        _connections.erase(id);
        return false;
    }

    const std::string& path = msg.msg->get_path();

    static const std::map<std::string_view, int> dirs {
        { "status", DIR_STATUS }, { "block", DIR_BLOCK }, { "blocks", DIR_BLOCKS }
    };

    const HttpConnection::Ptr& conn = it->second;

    bool (Server::*func)(const HttpConnection::Ptr&) = 0;

    if (_currentUrl.parse(path, dirs)) {
        switch (_currentUrl.dir) {
            case DIR_STATUS:
                func = &Server::send_status;
                break;
            case DIR_BLOCK:
                func = &Server::send_block;
                break;
            case DIR_BLOCKS:
                func = &Server::send_blocks;
                break;
            default:
                break;
        }
    }

    bool keepalive = false;

    if (func) {
        //bool validKey = _acl.check(_currentUrl.args["m"], _currentUrl.args["n"], _currentUrl.args["h"]);
        bool validKey = _acl.check(conn->peer_address());
        if (!validKey) {
            send(conn, 403, "Forbidden");
        } else {
            keepalive = (this->*func)(conn);
        }
    } else {
        send(conn, 404, "Not Found");
    }

    if (!keepalive) {
        conn->shutdown();
        _connections.erase(it);
    }
    return keepalive;
}

bool Server::send_status(const HttpConnection::Ptr& conn) {
    _body.clear();
    if (_backend.get_status(_body)) {
        return send(conn, 500, "Internal error #1");
    }
    return send(conn, 200, "OK");
}

bool Server::send_block(const HttpConnection::Ptr &conn) {
    auto height = _currentUrl.get_int_arg("height", 0);
    if (!_backend.get_block(_body, height)) {
        return send(conn, 500, "Internal error #2");
    }
    return send(conn, 200, "OK");
}

bool Server::send_blocks(const HttpConnection::Ptr& conn) {
    auto start = _currentUrl.get_int_arg("start", 0);
    auto end = _currentUrl.get_int_arg("end", 0);
    if (!_backend.get_blocks(_body, start, end)) {
        return send(conn, 500, "Internal error #3");
    }
    return send(conn, 200, "OK");
}

bool Server::send(const HttpConnection::Ptr& conn, int code, const char* message) {
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

namespace {

void sha(const std::string_view& left, const std::string_view& right, char* outBase16) {
    secp256k1_sha256_t ctx;
    secp256k1_sha256_initialize(&ctx);
    secp256k1_sha256_write(&ctx, (const unsigned char*)left.data(), left.size());
    secp256k1_sha256_write(&ctx, (const unsigned char*)right.data(), right.size());
    unsigned char out32[32];
    secp256k1_sha256_finalize(&ctx, out32);
    to_hex(outBase16, out32, 32);
}

} //namespace

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
    return _ips.count(peerAddress.ip());
}

/*
Server::AccessControl::AccessControl(const std::string &keysFileName) :
    _enabled(!keysFileName.empty()),
    _keysFileName(keysFileName),
    _lastModified(0)
{
    refresh();
}

void Server::AccessControl::refresh() {
    using namespace boost::filesystem;

    if (!_enabled) return;

    try {
        path p(_keysFileName);
        auto t = last_write_time(p);
        if (t <= _lastModified) {
            return;
        }
        _lastModified = t;
        std::ifstream file(_keysFileName);
        std::string line;
        char maskBuf[80];
        while (std::getline(file, line)) {
            boost::algorithm::trim(line);
            if (line.size() < 8) continue;
            sha(line, line, maskBuf);
            _keys[std::string(maskBuf)] = line;
        }
    } catch (std::exception& e) {
        LOG_ERROR() << e.what();
    }
}

bool Server::AccessControl::check(
    const std::string_view& mask, const std::string_view& nonce, const std::string_view& hash
) {
    if (!_enabled) return true;

    if (mask.size() != 64 || hash.size() != 64 || nonce.empty()) {
        return false;
    }
    std::string k(mask);
    auto it = _keys.find(k);
    if (it == _keys.end()) return false;
    char buf[80];
    sha(it->second, nonce, buf);
    return memcmp(hash.data(), buf, 64) == 0;
}
*/

}} //namespaces
