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
#include "utility/io/tcpstream.h"

namespace beam {

/// Reads-writes messages from-to connected stream
class BaseConnection {
public:
    enum Direction : uint8_t { inbound, outbound };

    virtual uint64_t id() const = 0;
    virtual void change_id(uint64_t newId) = 0;

    /// Writes fragments to stream
    io::Result write_msg(const io::SerializedMsg& fragments, bool flush=true)  {
        return _stream->write(fragments, flush);
    }

    /// Writes single msg to stream
    io::Result write_msg(const io::SharedBuffer& msg, bool flush=true)  {
        return _stream->write(msg, flush);
    }

    /// Shutdowns write side, waits for pending write requests to complete, but on reactor's side
    void shutdown()  {
        _stream->shutdown();
    }

    /// Returns socket address (non-null if connected)
    io::Address address() const {
        return _stream->address();
    }

    /// Returns peer address (non-null if connected)
    io::Address peer_address() const  {
        return _peerAddress;
    }

    /// Returns direction
    Direction direction() const {
        return _direction;
    }

protected:
    /// Ctor. Attaches connected tcp stream
    BaseConnection(Direction d, io::TcpStream::Ptr&& stream) :
        _direction(d),
        _stream(std::move(stream)),
        _peerAddress(_stream->peer_address())
    {}

    /// Dtor
    virtual ~BaseConnection() = default;

    const Direction _direction;
    io::TcpStream::Ptr _stream;
    io::Address _peerAddress; // keep it after disconnect
};

} //namespace
