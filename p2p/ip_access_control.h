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

    /// Unban-reconnect timer
    void on_timer();


private:
    /// Checks schedule and performs reconnect/unban if wait time expired
    /// returns true to proceed
    bool dequeue_schedule(Timestamp now);

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
