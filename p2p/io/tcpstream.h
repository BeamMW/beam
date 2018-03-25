#pragma once
#include "reactor.h"

namespace io {

class TcpStream : protected Reactor::Object {
public:
    // doesn't initialize internal stuff
    explicit TcpStream(Reactor::Ptr reactor);

    bool is_connected();

private:
    friend class TcpServer;

    // returns status code
    int accepted(uv_handle_t* acceptor);

};

} //namespace

