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
#include "peer_info.h"
#include <set>
#include <unordered_set>
#include <unordered_map>

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
