#pragma once
#include "tcpstream.h"
#include "address.h"

namespace beam { namespace io {

class TcpServer : protected Reactor::Object {
public:
    using Ptr = std::unique_ptr<TcpServer>;

    // Either newStream is accepted or status != 0
    using Callback = std::function<void(TcpStream::Ptr&& newStream, ErrorCode status)>;

    // TODO simplified API, will add more as soon as needed
    static Ptr create(const Reactor::Ptr& reactor, Address bindAddress, Callback&& callback);

private:
    TcpServer(Callback&& callback);

    void on_accept(ErrorCode errorCode);

    Callback _callback;
};

}} //namespaces

