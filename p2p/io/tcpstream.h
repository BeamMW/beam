#pragma once
#include "reactor.h"
#include <functional>

namespace io {

class TcpStream : protected Reactor::Object {
public:
    // doesn't initialize internal stuff
    explicit TcpStream(Reactor::Ptr reactor);

    bool is_connected();

private:
    friend class TcpServer;
    friend class TcpConnector;

    // returns status code
    int accepted(uv_handle_t* acceptor);

    void connected(uv_stream_t* handle);
};

} //namespace

