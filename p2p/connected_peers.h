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
#include "connection.h"
#include "notifications.h"
#include <unordered_map>

namespace beam {

class Protocol;

/// Active connections
class ConnectedPeers {
public:
    using OnConnectionRemoved = std::function<void(StreamId)>;

    ConnectedPeers(P2PNotifications& notifications, Protocol& protocol, OnConnectionRemoved removedCallback);

    uint32_t total_connected() const;

    void add_connection(Connection::Ptr&& conn);

    void update_peer_state(StreamId streamId, PeerState&& newState);

    void remove_connection(StreamId id);

    io::Result write_msg(StreamId id, const io::SharedBuffer& msg);

    void ping(const io::SharedBuffer& msg);

    void query_known_servers();

private:
    struct Info {
        Connection::Ptr conn;
        PeerState ps;
        Timestamp32 lastUpdated=0;
        bool knownServersChanged=false;
    };

    using Container = std::unordered_map<StreamId, Info>;

    void broadcast(const io::SharedBuffer& msg, std::function<bool(Info&)>&& filter);

    bool find(StreamId id, Container::iterator& it);

    P2PNotifications& _notifications;
    OnConnectionRemoved _removedCallback;
    io::SharedBuffer _knownServersQueryMsg;
    Container _connections;
    std::vector<StreamId> _toBeRemoved;
};

} //namespace
