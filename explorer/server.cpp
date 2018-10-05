#include "server.h"
#include "adapter.h"
#include "utility/logger.h"

namespace beam { namespace explorer {

namespace {

#define STS "Status server: "

static const uint64_t SERVER_RESTART_TIMER = 1;
static const unsigned SERVER_RESTART_INTERVAL = 1000;
static const size_t MAX_REQUEST_BODY_SIZE = 0;
static const size_t REQUEST_BODY_SIZE_THRESHOLD = 0;

enum Dirs {
    DIR_STATUS, DIR_BLOCKS
    // etc
};

} //namespace

Server::Server(IAdapter& adapter, io::Reactor& reactor, io::Address bindAddress) :
    _msgCreator(2000),
    _backend(adapter),
    _reactor(reactor),
    _timers(reactor, 100),
    _bindAddress(bindAddress)
{
    _timers.set_timer(SERVER_RESTART_TIMER, 0, BIND_THIS_MEMFN(start_server));
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
        { "status", DIR_STATUS }, { "blocks", DIR_BLOCKS }
    };

    bool ret = false;
    const HttpConnection::Ptr& conn = it->second;

    if (_currentUrl.parse(path, dirs)) {
        switch (_currentUrl.dir) {
            case DIR_STATUS:
                ret = send_status(conn);
                break;
            case DIR_BLOCKS:
                ret = send_blocks(conn);
                break;
            default:
                break;
        }
    }

    if (!ret) {
        send_404(conn);
        conn->shutdown();
        _connections.erase(it);
    }

    return ret;
}

bool Server::send_status(const HttpConnection::Ptr& conn) {
    return send(conn, 200, "OK", &_backend.get_status(_msgCreator));
}

bool Server::send_blocks(const HttpConnection::Ptr& conn) {
    auto start = _currentUrl.get_int_arg("start", -1);
    auto end = _currentUrl.get_int_arg("end", -1);
    if (start < 0 || end < 0 || end < start || end - start > 100) {
        return send_404(conn);
    }
/*
    io::SharedBuffer body;
        int code = 200;
        const char* message = "OK";
        bool stop = (path == "/stop");
        if (!stop && path != "/") {
            try {
                body = io::map_file_read_only(path.c_str());
            } catch (const std::exception& e) {
                LOG_DEBUG() << e.what();
                code = 404;
                message = "Not found";
            }
        }

        static const HeaderPair headers[] = {
            {"Server", "DummyHttpServer"},
            {"Host", msg.msg->get_header("host").c_str() }
        };

    */

return false;
}

bool Server::send_404(const HttpConnection::Ptr& conn) {
    return send(conn, 404, "Not Found", 0);
}

bool Server::send(const HttpConnection::Ptr& conn, int code, const char* message, const io::SharedBuffer* body) {
    assert(conn);

    bool ok = _msgCreator.create_response(
        _serialized,
        code,
        message,
        0, //headers,
        0, //sizeof(headers) / sizeof(HeaderPair),
        1,
        "application/json",
        body ? body->size : 0
    );

    if (ok) {
        if (body && !body->empty()) _serialized.push_back(*body);
        conn->write_msg(_serialized);

    } else {
        LOG_ERROR() << STS << "cannot create response";
        return false;
    }

    return (code == 200);
}

}} //namespaces
