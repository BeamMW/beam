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
