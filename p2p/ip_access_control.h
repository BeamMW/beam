#pragma once
#include "peer_info.h"
#include <map>
#include <set>
#include <unordered_set>

namespace beam {

/// Manages allow, ban, and reconnect policies
class IpAccessControl {
public:
    using AllowCallback = std::function<void(io::Address a)>;

    /// Ctor. If allowed list is not empty then allow policy comes into effect
    IpAccessControl(AllowCallback unbanCallback, AllowCallback reconnectCallback, std::unordered_set<uint32_t> allowedIps = std::unordered_set<uint32_t>());

    /// Returns if ip allowed at the moment
    bool is_ip_allowed(uint32_t ip);

    void schedule_reconnect(io::Address a, Timestamp waitUntil);

    void ban(io::Address a, Timestamp waitUntil);

    void unban(io::Address a);

/*
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
*/
private:
    using IpSet = std::unordered_set<uint32_t>;

    AllowCallback _unbanCallback;
    AllowCallback _reconnectCallback;
    bool _allowPolicy;
    IpSet _allowed;

    struct Info {
        struct Key {
            Timestamp waitUntil=0;
            uint32_t ip=0;

            bool operator<(const Key& k) const {
                return waitUntil < k.waitUntil;
            }
        };

        Key key;
        uint16_t port=0;
        bool isBanned=false;
    };

    using Schedule = std::set<Info::Key>;
    using IpToPeer = std::unordered_map<uint32_t, Info>;

    Schedule _schedule;
    IpToPeer _denied;
};

} //namespace
