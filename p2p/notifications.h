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
#include "types.h"

namespace beam {

struct P2PNotifications {
    virtual ~P2PNotifications() {}
    virtual void on_p2p_started(class P2P* p2p) = 0;
    virtual void on_peer_connected(StreamId id) = 0;
    virtual void on_peer_state_updated(StreamId id, const PeerState& newState) = 0;
    virtual void on_peer_disconnected(StreamId id) = 0;
    virtual void on_p2p_stopped() = 0;
};

} //namespace
