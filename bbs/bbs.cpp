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

#include "bbs.h"
#include "p2p/protocol.h"
#include "p2p/connection.h"
#include "p2p/types.h"
#include "utility/io/tcpserver.h"
#include "utility/asynccontext.h"
#include "utility/logger.h"
#include "utility/io/errorhandling.h"

namespace beam { namespace bbs {

class Server : public IErrorHandler, private AsyncContext {
public:
    Server(Config config, const std::vector<io::Address>& initialPeers) :
        _protocol(0, 0, 1, 10, *this, 0x2000),
        _config(config)
    {
        if (_config.serverAddress.port() == 0) {
            throw std::runtime_error("bbs: no listen port specified");
        }

        for (const auto& a : initialPeers) {
            _knownPeers[a] = 0;
        }

        _protocol.add_message_handler<Server, Config, &Server::on_config_msg>(CONFIG_MSG_TYPE, this, 2, 20);
        _protocol.add_message_handler<Server, Servers, &Server::on_servers_msg>(SERVERS_MSG_TYPE, this, 0, 2000000);
        _protocol.add_message_handler<Server, Bytes, &Server::on_message>(PUBLISH_MSG_TYPE, this, 0, 2000000);
        _protocol.add_message_handler<Server, Request, &Server::on_request>(REQUEST_MSG_TYPE, this, 0, 20);

        _configMsg = _protocol.serialize(CONFIG_MSG_TYPE, _config, true);

        _knownPeersSpectre = _config.spectre;
    }

    void start() {
        io::Result result = set_coarse_timer(1, 0, BIND_THIS_MEMFN(on_start_server));
        if (!result) IO_EXCEPTION(result.error());

        if (_config.historyDepth > 0) {
            result = set_coarse_timer(2, 10000, BIND_THIS_MEMFN(on_cleanup_history));
            if (!result) IO_EXCEPTION(result.error());
        }

        run_async();
    }

    ~Server() {
        stop();
        wait();
    }

private:
    enum StreamIdFlags { peer=16, subscribed=32 };

    void on_start_server(TimerID) {
        try {
            _thisServer = io::TcpServer::create(*_reactor, _config.serverAddress, BIND_THIS_MEMFN(on_stream_accepted));
        } catch (const io::Exception& e) {
            LOG_ERROR() << "tcp server error " << io::error_str(e.errorCode) << ", restarting in 1 second";
        }
        if (_thisServer) {
            LOG_INFO() << "listening to " << _config.serverAddress;
        } else {
            set_coarse_timer(1, 1000, BIND_THIS_MEMFN(on_start_server));
        }
    }

    void on_cleanup_history(TimerID) {
        if (!_history.empty()) {
            uint64_t key = history_key(time(0) - _config.historyDepth, 0);
            auto it = _history.begin();
            for (; it != _history.end(); ++it) {
                if (it->first > key) break;
            }
            _history.erase(_history.begin(), it);
        }
        set_coarse_timer(2, 10000, BIND_THIS_MEMFN(on_cleanup_history));
    }

    void on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode) {
        if (errorCode != io::EC_OK) {
            LOG_ERROR() << "tcp server error " << io::error_str(errorCode) << ", restarting in 1 second";
            _thisServer.reset();
            set_coarse_timer(2, 1000, BIND_THIS_MEMFN(on_start_server));
            return;
        }

        StreamId streamId(newStream->peer_address());
        streamId.fields.flags = StreamId::inbound;

        Connection::Ptr connection = std::make_unique<Connection>(
            _protocol,
            streamId.u64,
            Connection::inbound,
            10000, //TODO config
            std::move(newStream)
        );

        io::Result result = connection->write_msg(_configMsg);
        if (!result) {
            LOG_WARNING() << "failed to send config message to new connection " << connection->peer_address()
                << "error=" << io::error_str(result.error());
        } else {
            LOG_INFO() << "new connection from " << connection->peer_address();
            _newConnections[streamId] = std::move(connection);
        }
    }

    void on_protocol_error(uint64_t from, ProtocolError error) override {
        StreamId streamId(from);
        LOG_WARNING() << "protocol error " << error << " from " << streamId.address();
        cleanup_connection(streamId, true);
    }

    void on_connection_error(uint64_t from, io::ErrorCode errorCode) override {
        StreamId streamId(from);
        LOG_WARNING() << "connection error " << io::error_str(errorCode) << " from " << streamId.address();
        cleanup_connection(streamId, false);
    }

    void cleanup_connection(StreamId streamId, bool isProtoError) {
        uint16_t flags = streamId.flags();
        if (flags & peer) {
            _peers.erase(streamId);
            if ( (flags & StreamId::outbound) && !isProtoError) {
                // TODO reconnect or not
            }
        } else if (flags & subscribed) {
            _subscribed.erase(streamId);
        } else {
            _newConnections.erase(streamId);
        }
    }

    bool on_config_msg(uint64_t from, Config&& config) {
        StreamId streamId(from);

        Connections::iterator it = _newConnections.find(streamId);
        if (it == _newConnections.end()) {
            LOG_WARNING() << "unexpected config message from " << streamId.address();
            return false;
        }

        // Config is sent by peers only
        streamId.fields.flags |= peer;

        // TODO bool needRequest = need_to_request_known_servers();

        _peers[streamId] = std::move(it->second);
        _newConnections.erase(it);

        io::Address sa = config.serverAddress;
        if (sa.ip() == 0) sa.ip(streamId.address().ip());
        add_known_server(sa, config.spectre);

        return true;
    }

    bool on_servers_msg(uint64_t /*from*/, Servers&& servers) {
        for (const auto& p : servers) {
            add_known_server(p.first, p.second);
        }
        return true;
    }

    void add_known_server(io::Address address, uint8_t spectre) {
        auto& sp = _knownPeers[address];
        if (sp != spectre) {
            LOG_INFO() << "new known peer " << address;
            sp = spectre;
            _knownPeersSpectre |= spectre;
            _serversUpdated = false;
        }
    }

    bool on_message(uint64_t from, Bytes&& bytes) {
        StreamId streamId(from);
        Message message;
        message.seqNumber = ++_seq;
        message.timestamp = time(0);
        message.bytes = std::move(bytes);

        io::SharedBuffer serialized = _protocol.serialize(MESSAGE_MSG_TYPE, message, false);
        if (_config.historyDepth > 0) {
            if (_lastTimestamp != message.timestamp) {
                _lastTimestamp = message.timestamp;
                _localSeq = 0;
            } else {
                ++_localSeq;
            }
            _history[history_key(_lastTimestamp, _localSeq)] = serialized;
        }
        _cleanupCache.clear();
        for (auto& p : _subscribed) {
            if (p.first == streamId) continue;
            io::Result res = p.second->write_msg(serialized);
            if (!res) {
                LOG_INFO() << "client disconnected, address=" << p.first.address()
                    << ", error=" << io::error_str(res.error());;
                _cleanupCache.push_back(p.first);
            }
        }
        return true;
    }

    bool on_request(uint64_t from, Request&& request) {
        StreamId streamId(from);
        switch (request.action) {
            case Request::subscribe:
                return on_subscribe(streamId, request);
            case Request::unsubscribe:
                return on_unsubscribe(streamId, request);
            case Request::get_servers:
                return on_get_servers(streamId, request);
            default:
                LOG_WARNING() << "unexpected request " << request.action << " from " << streamId.address();
                cleanup_connection(streamId, true);
                break;
        }
        return false;
    }

    bool on_subscribe(StreamId streamId, const Request& request) {
        // TODO
        return true;
    }

    bool on_unsubscribe(StreamId streamId, const Request& request) {
        // TODO
        return true;
    }

    bool on_get_servers(StreamId streamId, const Request& request) {
        if (!_serversUpdated) {
            _protocol.serialize(_serversMsg, SERVERS_MSG_TYPE, _knownPeers);
            _serversUpdated = true;
        }

        // TODO


        return true;
    }

    bool need_to_request_known_servers() {
        static const size_t threshold = 40;
        if (_knownPeers.size() <= threshold) return true;
        if (_knownPeersSpectre == 0xFF) return false;

        return (rand() % 3 == 0);
    }

    uint64_t history_key(uint32_t timestamp, uint32_t localSeq) {
        uint64_t key(timestamp);
        key = (key << 32) + localSeq;
        return key;
    }

    // (timestamp << 32 + internal seq #) -> serialized msgs
    std::map<uint64_t, io::SharedBuffer> _history;
    uint32_t _seq=0;
    uint32_t _localSeq=0;
    uint32_t _lastTimestamp=0;
    Protocol _protocol;
    Config _config;
    io::SharedBuffer _configMsg;
    Servers _knownPeers;
    uint8_t _knownPeersSpectre=0;
    io::TcpServer::Ptr _thisServer;

    using Connections = std::unordered_map<StreamId, Connection::Ptr>;

    Connections _newConnections;
    Connections _clients;
    Connections _subscribed;
    Connections _peers;
    std::vector<StreamId> _cleanupCache;

    SerializedMsg _serversMsg;
    bool _serversUpdated=false;
};

}} //namespaces

int main(int argc, char* argv[]) {
    srand(time(0));

    using namespace beam;

    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = Logger::create(logLevel, logLevel, LOG_LEVEL_INFO, "bbs_");
    logger->set_header_formatter(
        [](char* buf, size_t maxSize, const char* timestampFormatted, const LogMessageHeader& header) -> size_t {
            if (header.line)
                return snprintf(buf, maxSize, "%c %s (%s) ", loglevel_tag(header.level), timestampFormatted, header.func);
            return snprintf(buf, maxSize, "%c %s ", loglevel_tag(header.level), timestampFormatted);
        }
    );
    logger->set_time_format("%T", true);

    if (argc < 4) {
        LOG_ERROR() << "Usage: ./bbs PORT SPECTRE DEPTH [CONNECT_TO]";
        return 1;
    }

    bbs::Config config;
    config.serverAddress.port(atoi(argv[1]));
    config.spectre = strtol(argv[2], 0, 16);
    config.historyDepth = atoi(argv[3]);

    std::vector<io::Address> connectTo;

    if (argc >= 5) {
        io::Address addr;
        if (!strchr(argv[4], ':') || !addr.resolve(argv[4]) || addr.ip() == 0) {
            LOG_ERROR() << "Invalid peer address " << argv[4];
            return 1;
        }
        connectTo.push_back(addr);
    }

    try {
        bbs::Server server(config, connectTo);
        server.start();
        wait_for_termination(0);
        return 0;
    } catch (const std::exception& e) {
        LOG_ERROR() << "Exception: " << e.what();
    } catch (...) {
        LOG_ERROR() << "Non-std exception";
    }

    return 127;
}
