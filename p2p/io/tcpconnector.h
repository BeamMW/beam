#pragma once
#include "tcpstream.h"
#include <unordered_map>

namespace io {

class TcpConnector {
public:
    using ID = void*;

    // Either newStream is connected or errorCode != 0
    using Callback = std::function<void(ID id, TcpStream&& newStream, int errorCode)>;

    TcpConnector(Reactor::Ptr reactor);
    ~TcpConnector();

    // returns ID
    // TODO: add timeouts, etc
    ID connect(std::string ip, uint16_t port, Callback&& callback);

    void cancel(ID id);

private:
    struct Context {
        Callback callback;
        uv_connect_t request;
    };

    void internal_callback(uv_stream_t* handle, int status);

    Reactor::Ptr _reactor;
    std::unordered_map<ID, Context> _connections;
};

} //namespace
