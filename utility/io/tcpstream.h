#pragma once
#include "reactor.h"
#include "config.h"
#include "bufferchain.h"
#include "reactor.h"

namespace beam { namespace io {

class TcpStream : protected Reactor::Object {
public:
    //using Ptr = std::shared_ptr<TcpStream>;
    using Ptr = std::unique_ptr<TcpStream>;

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

    struct State {
        uint64_t received=0;
        uint64_t sent=0;
        size_t unsent=0; // == _writeBuffer.size()
    };

    // Sets callback and enables reading from the stream if callback is not empty
    // returns false if stream disconnected
    bool enable_read(const Callback& callback);

    bool disable_read();

    /// Writes raw data, returns status code
    int write(const void* data, size_t size) {
        return write(SharedBuffer(data, size));
    }

    /// Writes raw data, returns status code
    int write(const SharedBuffer& buf);

    /// Writes raw data, returns status code
    int write(const std::vector<SharedBuffer>& fragments);

    /// Writes raw data, returns status code
    int write(const BufferChain& buf);

    bool is_connected() const;

    void close();

    const State& state() const {
        return _state;
    }

    Address peer_address() const;

    int get_last_error() const { return _lastError; }

private:
    friend class TcpServer;
    friend class Reactor;

    TcpStream() = default;

    // sends async write request
    int send_write_request();

    // callback from write request
    void on_data_written(int status);

    // returns status code
    int accepted(uv_handle_t* acceptor);

    void connected(uv_stream_t* handle);

    std::vector<char> _readBuffer;
    BufferChain _writeBuffer;
    Callback _callback;
    State _state;
    uv_write_t _writeRequest;
    bool _writeRequestSent=false;
    int _lastError=0;
};

}} //namespaces

