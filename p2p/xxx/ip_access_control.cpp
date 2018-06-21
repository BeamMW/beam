#include "ip_access_control.h"
#include "utility/helpers.h"
#include "utility/logger.h"
#include <time.h>

namespace beam {

IpAccessControl::IpAccessControl(
    IpAccessControl::AllowCallback unbanCallback,
    IpAccessControl::AllowCallback reconnectCallback,
    std::unordered_set<uint32_t> allowedIps)
:
    _unbanCallback(std::move(unbanCallback)),
    _reconnectCallback(std::move(reconnectCallback)),
    _allowPolicy(!allowedIps.empty()),
    _allowed(allowedIps)
    // TODO
{
    assert(_unbanCallback && _reconnectCallback);
}

bool IpAccessControl::is_ip_allowed(uint32_t ip) {
    if (_allowPolicy && !_allowed.count(ip)) {
        return false;
    }

    auto it = _denied.find(ip);
    if (it == _denied.end()) {
        return true;
    }

    const Info& i = it->second;
    if (i.isBanned) {
        return false;
    }

    // remove peer from reconnect schedule in case of inbound connection from the same ip
    _schedule.erase(i.key);
    _denied.erase(it);

    return true;
}

void IpAccessControl::schedule_reconnect(io::Address a, Timestamp waitUntil) {
    if (waitUntil == 0) return;

    uint16_t port = a.port();
    uint32_t ip = a.ip();
    if (port == 0 || ip == 0) {
        LOG_ERROR() << "Ignoring reconnect to address " << a;
        return;
    }

    Info& i = _denied[ip];
    if (i.isBanned) {
        LOG_ERROR() << "Ignoring reconnect to banned address " << a;
        return;
    }

    if (i.key.waitUntil != 0) {
        // reschedule
        _schedule.erase(i.key);
    }

    i.key.waitUntil = waitUntil;
    i.key.ip = ip;
    i.port = port;
    _schedule.insert(i.key);
}

void IpAccessControl::ban(io::Address a, Timestamp waitUntil) {
    if (waitUntil == 0) return;

    uint32_t ip = a.ip();
    if (ip == 0) {
        LOG_ERROR() << "Ignoring banning empty ip";
        return;
    }

    Info& i = _denied[ip];
    if (i.isBanned) {
        LOG_WARNING() << "Ignoring already banned ip " << a.port(0);
        return;
    }

    if (i.key.waitUntil != 0) {
        // reschedule
        _schedule.erase(i.key);
    }

    LOG_INFO() << "Banning address " << a << " until " << format_timestamp("%Y-%m-%d.%T", waitUntil*1000, false);

    if (_allowPolicy) {
        _allowed.erase(ip);
    }

    i.key.waitUntil = waitUntil;
    i.key.ip = ip;
    i.port = a.port();
    _schedule.insert(i.key);
}

void IpAccessControl::unban(io::Address a) {
    uint32_t ip = a.ip();
    if (ip == 0) return;

    auto it = _denied.find(ip);
    if (it == _denied.end() || !it->second.isBanned) {
        LOG_WARNING() << "Ignoring not banned ip " << a.port(0);
        return;
    }

    LOG_INFO() << "Unbanning ip " << a.ip();

    if (_allowPolicy) {
        _allowed.insert(ip);
    }

    _schedule.erase(it->second.key);
    _denied.erase(it);
}

void IpAccessControl::on_timer() {
    if (_schedule.empty()) {
        return;
    }
    Timestamp now = time(0);
    while (dequeue_schedule(now)) {}
}

bool IpAccessControl::dequeue_schedule(Timestamp now) {
    auto i = _schedule.begin();
    if (i == _schedule.end() || i->waitUntil > now) {
        return false;
    }
    uint32_t ip = i->ip;
    _schedule.erase(i);
    auto j = _denied.find(ip);
    if (j != _denied.end()) {
        Info& info = j->second;
        io::Address a(ip, info.port);
        if (info.isBanned) {
            _unbanCallback(a);
        } else {
            _reconnectCallback(a);
        }
        _denied.erase(j);
    }
    return true;
}

} //namespace
