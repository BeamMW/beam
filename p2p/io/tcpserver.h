#pragma once
#include "tcpstream.h"
#include <functional>

namespace io {

class TcpServer : protected Reactor::Object {
public:
    // Either newStream is accepted or errorCode != 0
    using Callback = std::function<void(TcpStream&& newStream, int errorCode)>;

    // TODO simplified API, will add more as soon as needed
    TcpServer(Reactor::Ptr reactor, uint16_t port, Callback&& callback, bool listenToLocalhostOnly=false);

    ~TcpServer();
private:
    void internal_callback(int status);

    Callback _callback;


};

} //namespace

