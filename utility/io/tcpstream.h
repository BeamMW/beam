#pragma once
#include "reactor.h"
#include "bufferchain.h"

namespace beam { namespace io {

class TcpStream : protected Reactor::Object {
public:
    //using Ptr = std::shared_ptr<TcpStream>;
    using Ptr = std::unique_ptr<TcpStream>;

    // TODO consider 2 more options for read buffer if it appears cheaper to avoid
    // double copying:
    // 1) call back with sub-chunk of shared memory
    // 2) embed deserializer (protocol-specific) object into stream

    // errorCode==0 on new data
    using Callback = std::function<void(ErrorCode errorCode, void* data, size_t size)>;

    struct State {
        uint64_t received=0;
        uint64_t sent=0;
        size_t unsent=0; // == _writeBuffer.size()
    };

    ~TcpStream();

    // Sets callback and enables reading from the stream if callback is not empty
    // returns false if stream disconnected
    expected<void, ErrorCode> enable_read(const Callback& callback);

    void disable_read();

    /// Writes raw data, returns status code
    expected<void, ErrorCode> write(const void* data, size_t size) {
        return write(SharedBuffer(data, size));
    }

    /// Writes raw data, returns status code
    expected<void, ErrorCode> write(const SharedBuffer& buf);

    /// Writes raw data, returns status code
    expected<void, ErrorCode> write(const std::vector<SharedBuffer>& fragments);

    /// Writes raw data, returns status code
    expected<void, ErrorCode> write(const BufferChain& buf);

    bool is_connected() const;

    void close();

    const State& state() const {
        return _state;
    }

    Address peer_address() const;

private:
    static void on_read(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf);

    friend class TcpServer;
    friend class Reactor;

    TcpStream() = default;
    
    void alloc_read_buffer();
    void free_read_buffer();

    // sends async write request
    expected<void, ErrorCode> send_write_request();

    // callback from write request
    void on_data_written(ErrorCode errorCode);

    // stream accepted from server
    ErrorCode accepted(uv_handle_t* acceptor);

    void connected(uv_stream_t* handle);

    uv_buf_t _readBuffer={0, 0};
    BufferChain _writeBuffer;
    Callback _callback;
    State _state;
    uv_write_t _writeRequest;
    bool _writeRequestSent=false;
};

}} //namespaces

