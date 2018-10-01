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

#include "p2p.h"
#include "utility/config.h"
#include "utility/logger.h"

namespace beam {

enum TimerIDs {
    SERVER_RESTART_TIMER = 1,
    CONNECT_TIMER = 2,
    PING_TIMER=3,
    QUERY_KNOWN_SERVERS_TIMER=4
};

P2P::P2P(P2PNotifications& notifications, P2PSettings settings) :
    _notifications(notifications),
    _settings(std::move(settings)),
    _connectPool(_rdGen),
    _protocol(0xAA, 0xBB, 0xCC, 100, *this, 0x2000),
    _handshakes(_connectPool, BIND_THIS_MEMFN(on_peer_handshaked), BIND_THIS_MEMFN(on_handshake_error)),
    _connectedPeers(_notifications, _protocol, BIND_THIS_MEMFN(on_peer_removed))
{
    if (_settings.peerId == 0) {
        _settings.peerId = _rdGen.rnd<uint64_t>();
        LOG_INFO() << "initializing this peer id: " << std::hex << _settings.peerId << std::dec;
        // TODO save settings
    }
    _connectPool.setup(_settings.peerId, io::Address(_settings.bindToIp, _settings.listenToPort), _settings.priorityPeers);
    _handshakes.setup(_protocol, _settings.listenToPort, _settings.peerId);

    _protocol.add_message_handler<P2P, PeerState, &P2P::on_peer_state>(PEER_STATE_MSG_TYPE, this, 2, 200);
    _protocol.add_message_handler<P2P, VoidMessage, &P2P::on_known_servers_request>(KNOWN_SERVERS_REQUEST_MSG_TYPE, this, 0, 0);
    _protocol.add_message_handler<P2P, KnownServers, &P2P::on_known_servers>(KNOWN_SERVERS_MSG_TYPE, this, 0, 2000000);
}

P2P::~P2P() {
    stop();
    wait();
}

void P2P::start() {
    io::Result result;
    if (_settings.listenToPort != 0) {
        result = set_coarse_timer(SERVER_RESTART_TIMER, 100 /*TODO this was test 0*/, BIND_THIS_MEMFN(on_start_server));
        if (!result) IO_EXCEPTION(result.error());
    }
    result = set_coarse_timer(CONNECT_TIMER, 0, BIND_THIS_MEMFN(on_connect_to_peers));
    if (!result) IO_EXCEPTION(result.error());
    result = set_coarse_timer(PING_TIMER, _settings.pulsePeriodMsec * 2, BIND_THIS_MEMFN(on_ping_timer));
    if (!result) IO_EXCEPTION(result.error());
    result = set_coarse_timer(QUERY_KNOWN_SERVERS_TIMER, _settings.pulsePeriodMsec * 5/2, BIND_THIS_MEMFN(on_known_servers_timer));
    if (!result) IO_EXCEPTION(result.error());
    run_async([this]{ _notifications.on_p2p_started(this); }, [this]{ _notifications.on_p2p_stopped(); });
}

void P2P::update_tip(uint32_t newTip) {
    _peerState.tip = newTip;
    _peerStateUpdated = true;
}

void P2P::on_protocol_error(uint64_t id, ProtocolError error) {
    StreamId streamId(id);
    LOG_WARNING() << "protocol error " << error << " from " << streamId.address() << ", closing connection";
    cleanup_connection(streamId);
}

void P2P::on_connection_error(uint64_t id, io::ErrorCode errorCode) {
    StreamId streamId(id);
    LOG_WARNING() << "connection error " << io::error_str(errorCode) << " from " << streamId.address() << " flags " << streamId.flags();
    cleanup_connection(streamId);
    if (streamId.flags() & StreamId::outbound) {
        _connectPool.schedule_reconnect(streamId.address(), errorCode);
    }
}

void P2P::cleanup_connection(StreamId streamId) {
    uint16_t flags = streamId.flags();
    if (flags & StreamId::handshaking) {
        _handshakes.on_disconnected(streamId);
    } else if (flags & StreamId::active) {
        _connectPool.release_peer_id(streamId.address());
        _connectedPeers.remove_connection(streamId);
    }
}

void P2P::on_start_server(TimerID) {
    io::Address listenTo(_settings.bindToIp, _settings.listenToPort);
    try {
        _thisServer = io::TcpServer::create(_reactor, listenTo, BIND_THIS_MEMFN(on_stream_accepted));
    } catch (const io::Exception e) {
        LOG_ERROR() << "tcp server error " << io::error_str(e.errorCode) << ", restarting in 1 second";
    }
    if (_thisServer) {
        LOG_INFO() << "listening to " << listenTo;
    } else {
        set_coarse_timer(SERVER_RESTART_TIMER, 1000, BIND_THIS_MEMFN(on_start_server));
    }
}

void P2P::on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode) {
    if (errorCode != 0) {
        LOG_ERROR() << "tcp server error " << io::error_str(errorCode) << ", restarting in 1 second";
        _thisServer.reset();
        set_coarse_timer(SERVER_RESTART_TIMER, 1000, BIND_THIS_MEMFN(on_start_server));
        return;
    }

    assert(newStream);

    _handshakes.on_new_connection(
        std::make_unique<Connection>(
            _protocol,
            StreamId(newStream->peer_address()).u64,
            Connection::inbound,
            10000, //TODO config
            std::move(newStream)
        )
    );
}

void P2P::on_connect_to_peers(TimerID) {
    auto connectTo = _connectPool.get_connect_candidates();

    if (!connectTo.empty()) {
        LOG_DEBUG() << "connecting to " << connectTo.size() << " peers";

        for (auto& a: connectTo) {
            StreamId id(a);
            auto result = _reactor->tcp_connect(a, id.u64, BIND_THIS_MEMFN(on_stream_connected), 10000, io::Address(_settings.bindToIp, 0));
            if (!result) {
                _connectPool.schedule_reconnect(a, result.error());
            } else {
                LOG_INFO() << "connecting to " << a;
            }
        }
    }

    set_coarse_timer(CONNECT_TIMER, _settings.pulsePeriodMsec, BIND_THIS_MEMFN(on_connect_to_peers));
}

void P2P::on_stream_connected(uint64_t id, io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode) {
    if (errorCode != 0) {
         _connectPool.schedule_reconnect(StreamId(id).address(), errorCode);
        return;
    }

    _handshakes.on_new_connection(
        std::make_unique<Connection>(
            _protocol,
            StreamId(newStream->peer_address()).u64,
            Connection::outbound,
            10000, //TODO config
            std::move(newStream)
        )
    );
}

void P2P::on_peer_handshaked(Connection::Ptr&& conn, bool isServer) {
    if (isServer) {
        io::Address addr = StreamId(conn->id()).address();
        auto p = _knownServers.insert(addr);
        if (p.second) {
            _peerState.knownServersCount = static_cast<uint32_t>(_knownServers.size());
            _peerStateUpdated = true;
            _knownServersUpdated = true;
        }
    }

    StreamId streamId = conn->id();
    streamId.fields.flags |= StreamId::active;
    conn->change_id(streamId.u64);
    conn->enable_all_msg_types();
    conn->disable_msg_type(HANDSHAKE_MSG_TYPE);
    conn->disable_msg_type(HANDSHAKE_ERROR_MSG_TYPE);
    _connectedPeers.add_connection(std::move(conn));
    _peerState.connectedPeersCount++;
    _peerStateUpdated = true;
}

void P2P::on_handshake_error(StreamId streamId, const HandshakeError& e) {
    LOG_ERROR() << "handshake with " << streamId.address() << " failed, " << e.str();
}

void P2P::on_peer_removed(StreamId streamId) {
    _connectPool.release_peer_id(streamId.address());
    _peerState.connectedPeersCount--;
    _peerStateUpdated = true;
}

void P2P::on_ping_timer(TimerID) {
    if (_connectedPeers.total_connected() > 0) {
        LOG_DEBUG() << "sending ping to connected peers";
        if (_peerStateUpdated) {
            _peerStateMsg = _protocol.serialize(PEER_STATE_MSG_TYPE, _peerState, false);
            _peerStateUpdated = false;
        }
        _connectedPeers.ping(_peerStateMsg);
    }
    set_coarse_timer(PING_TIMER, _settings.pulsePeriodMsec * 2, BIND_THIS_MEMFN(on_ping_timer));
}

bool P2P::on_peer_state(uint64_t id, PeerState&& state) {
    StreamId streamId(id);
    LOG_DEBUG() << "ping from " << streamId.address() << " " << streamId.flags();
    _connectedPeers.update_peer_state(streamId, std::move(state));
    return true;
}

void P2P::on_known_servers_timer(TimerID) {
    _connectedPeers.query_known_servers();
    set_coarse_timer(QUERY_KNOWN_SERVERS_TIMER, _settings.pulsePeriodMsec * 2, BIND_THIS_MEMFN(on_known_servers_timer));
}

bool P2P::on_known_servers_request(uint64_t id, VoidMessage&&) {
    StreamId streamId(id);
    LOG_DEBUG() << "known servers request from " << streamId.address();
    if (_knownServersUpdated) {
        _knownServersMsg = _protocol.serialize(KNOWN_SERVERS_MSG_TYPE, _knownServers, false);
        _knownServersUpdated = false;
    }
    return bool(_connectedPeers.write_msg(streamId, _knownServersMsg));
}

bool P2P::on_known_servers(uint64_t id, KnownServers&& servers) {
    StreamId streamId(id);
    LOG_DEBUG() << "known servers list from " << streamId.address() << " total = " << servers.size();
    io::Address thisServer(_settings.bindToIp, _settings.listenToPort);
    for (const auto& addr: servers) {
        if (addr == thisServer) {
            LOG_DEBUG() << "this server address received, " << thisServer;
            continue;
        }
        auto p = _knownServers.insert(addr);
        if (p.second) {
            LOG_INFO() << "new known server " << addr;
            _peerState.knownServersCount++;
            _knownServersUpdated = true;
            _peerStateUpdated = true;

            // TODO weights..
            _connectPool.add_connect_candidate(addr, 1);
        }
    }
    return true;
}

} //namespace
