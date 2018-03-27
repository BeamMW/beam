#pragma once
#include "reactor.h"
#include "config.h"
#include <functional>

namespace io {

class TcpStream : protected Reactor::Object {
public:
    // TODO hide library-specifics
    // errors<=>description platform-independent
    static const int END_OF_STREAM = UV_EOF;
    static const int WRITE_BUFFER_OVERFLOW = -100500;

    // TODO consider 2 more options for read buffer if it appears cheaper to avoid
    // double copying:
    // 1) call back with sub-chunk of shared memory
    // 2) embed deserializer (protocol-specific) object into stream

    // what==0 on new data
    using Callback = std::function<void(int what, void* data, size_t size)>;

    // doesn't initialize internal stuff
    TcpStream(Reactor::Ptr reactor, const Config& config);

    // Sets callback and enables reading from the stream if callback is not empty
    // returns false if stream disconnected
    bool enable(Callback&& callback);

    // TODO: iovecs[], shared buffers to immutable memory
    size_t try_write(const void* data, size_t size);

    bool is_connected();

    void close();

private:
    friend class TcpServer;
    friend class TcpConnector;

    // returns status code
    int accepted(uv_handle_t* acceptor);

    void connected(uv_stream_t* handle);

    std::vector<char> _readBuffer;
    Callback _callback;
};

} //namespace

