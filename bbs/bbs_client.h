#pragma once
#include "bbs.h"

namespace beam { namespace bbs {

// BBS client connection API
class Client {
public:
    // connection status callback, EC_OK means "connected"
    using OnConnectionStatus = std::function<void(io::ErrorCode)>;

    // message callback
    using OnMessage = std::function<void(const Message&)>;

    // peers list callback
    using OnPeerList = std::function<void(const Servers&)>;

    static std::unique_ptr<Client> create();

    virtual ~Client() {}
    virtual void connect(io::Address address, OnConnectionStatus connectionCallback) = 0;
    virtual void disconnect() = 0;
    virtual void publish(const void* buf, size_t size) = 0;
    virtual void request_peer_list(OnPeerList peerListCallback) = 0;
    virtual void subscribe(OnMessage messageCallback, uint32_t historyDepthSec) = 0;
    virtual void get_history(OnMessage messageCallback, uint32_t historyDepthStart, uint32_t historyDepthEnd) = 0;
};

}} //namespaces
