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
#include "utility/io/base_connection.h"
#include "msg_reader.h"

namespace beam {

/// Reads-writes messages from-to connected stream
class Connection : public BaseConnection {
public:
    using Ptr = std::unique_ptr<Connection>;

    /// Attaches connected tcp stream to protocol
    Connection(ProtocolBase& protocol, uint64_t peerId, Direction d, size_t defaultMsgSize, io::TcpStream::Ptr&& stream) :
        BaseConnection(d, std::move(stream)),
        _msgReader(protocol, peerId, defaultMsgSize)
    {
        _stream->enable_read(
            [this](io::ErrorCode what, void* data, size_t size) -> bool
            { return _msgReader.new_data_from_stream(what, data, size); }
        );
    }

    uint64_t id() const override { return _msgReader.id(); }
    void change_id(uint64_t newId) override { _msgReader.change_id(newId); }

    /// Allows receiving messages of given type
    void enable_msg_type(MsgType type) { _msgReader.enable_msg_type(type); }

    /// Allows receiving of all msg types
    void enable_all_msg_types() { _msgReader.enable_all_msg_types(); }

    /// Disables receiving messages of given type
    void disable_msg_type(MsgType type) { _msgReader.disable_msg_type(type); }

    /// Disables all messages
    void disable_all_msg_types() { _msgReader.disable_all_msg_types(); }

private:
    MsgReader _msgReader;
};

} //namespace
