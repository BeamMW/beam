#pragma once
#include "peer_info.h"
#include <map>
#include <unordered_map>

namespace beam {

class BannedPeers {
public:
    using Callback = std::function<void(Peer p, bool isBanned)>;

    BannedPeers(const Callback& callback) : _callback(callback) {
        assert(_callback);
    }

    bool is_banned(Peer p) const {
        return _peers.count(p) != 0;
    }

    void ban(Peer p, Timestamp t) {
        if (t == 0) return;
        PeerToTime::iterator p2t = _peers.find(p);
        TimeToPeer::iterator t2p = _schedule.insert( {t, p} );
        if (p2t != _peers.end()) {
            _schedule.erase(p2t->second);
            p2t->second = t2p;
        } else {
            _peers.insert( { p, t2p });
            _callback(p, true);
        }
    }

    void unban(Peer p) {
        PeerToTime::iterator it = _peers.find(p);
        if (it != _peers.end()) {
            _schedule.erase(it->second);
            _peers.erase(it);
            _callback(p, false);
        }
    }

    void unban_if_expired(Timestamp now) {
        TimeToPeer::iterator it = _schedule.begin();
        TimeToPeer::iterator e = _schedule.end();
        std::vector<Peer> unbanned;
        for (; it != e; ++it) {
            if (it->first <= now) {
                _peers.erase(it->second);
                unbanned.push_back(it->second);
            } else {
                break;
            }
        }
        _schedule.erase(_schedule.begin(), it);
        for (auto p: unbanned) {
            _callback(p, false);
        }
    }

private:
    using TimeToPeer = std::multimap<Timestamp, Peer>;
    using PeerToTime = std::unordered_map<Peer, TimeToPeer::iterator>;

    Callback _callback;
    TimeToPeer _schedule;
    PeerToTime _peers;
};

} //namespace
