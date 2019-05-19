// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
    using Callback = std::function<bool(ErrorCode errorCode, void* data, size_t size)>;

    struct State {
        uint64_t received=0;
        uint64_t sent=0;
        size_t unsent=0;
    };

    ~TcpStream();

    // Sets callback and enables reading from the stream if callback is not empty
    // returns false if stream disconnected
    Result enable_read(const Callback& callback);

    /// Disables listening to data and events
    void disable_read();

    /// Writes raw data, returns status code
    Result write(const void* data, size_t size, bool flush=true) {
        return write(SharedBuffer(data, size), flush);
    }

    /// Writes raw data, returns status code
    virtual Result write(const SharedBuffer& buf, bool flush=true);

    /// Writes raw data, returns status code
    virtual Result write(const SerializedMsg& fragments, bool flush=true);

    /// Writes raw data, returns status code
    //virtual Result write(const BufferChain& fragments, bool flush=true);

    /// Shutdowns write side, waits for pending write requests to complete, but on reactor's side
    virtual void shutdown();

    bool is_connected() const;

    const State& state() const {
        return _state;
    }

    /// Returns socket address (non-null if connected)
    Address address() const;

    /// Returns peer address (non-null if connected)
    Address peer_address() const;

    /// Enables tcp keep-alive
    void enable_keepalive(unsigned initialDelaySecs);

protected:
    TcpStream();

    /// read callback, returns whether to proceed
    virtual bool on_read(ErrorCode errorCode, void* data, size_t size);

private:
    static void read_cb(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf);

    friend class TcpServer;
    friend class SslServer;
    friend class Reactor;
    friend class TcpConnectors;

    void alloc_read_buffer();
    void free_read_buffer();

    // sends async write request if flush == true
    Result do_write(bool flush);

    // callback from write request
    void on_data_written(ErrorCode errorCode, size_t n);

    uv_buf_t _readBuffer={0, 0};
    BufferChain _writeBuffer;
    Callback _callback;
    State _state;
    Reactor::OnDataWritten _onDataWritten;
};

}} //namespaces

