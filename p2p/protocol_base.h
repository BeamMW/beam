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
#include "utility/io/buffer.h"
#include "utility/io/errorhandling.h"
#include <string.h>
#include <vector>

namespace beam {

using io::SerializedMsg;

/// Protocol can handle max 256 message types
using MsgType = uint8_t;

/// Message header of fixed size == 8
struct MsgHeader {
    /// Serialized header size
    static constexpr size_t SIZE = 8;

    /// Protocol version
    uint8_t V0, V1, V2;

    /// Message type, 1 byte
    MsgType type;

    /// Size of underlying serialized message
    uint32_t size;

    /// To be written to stream
    MsgHeader(uint8_t _v0, uint8_t _v1, uint8_t _v2, MsgType _type=0, uint32_t _size=0) :
        V0(_v0),
        V1(_v1),
        V2(_v2),
        type(_type),
        size(_size)
    {}

    /// Reuse ctor
    void reset(MsgType _type = 0, uint32_t _size=0) {
        type = _type;
        size = _size;
    }

    /// Reading from stream
    explicit MsgHeader(const void* data) {
        read(data);
    }

    /// Reads from stream, verifies header
    /// NOTE: caller makes sure that src points to at least SIZE bytes, Little endian
    void read(const void* src) {
        static_assert(sizeof(MsgHeader) == MsgHeader::SIZE);
#ifdef __BYTE_ORDER__
        static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "Only little endian supported yet");
#endif
        memcpy(this, src, SIZE);
    }

    /// Serializes into memory
    void write(void* dst) {
        memcpy(dst, this, SIZE);
    }
};

/// Zero sized message placeholder
struct VoidMessage {
    template<typename A> void serialize(A&) const {}
    template<typename A> void serialize(A&) {}
};

/// Errors occured during deserialization
enum ProtocolError {
    no_error = 0,               // ok
    protocol_version_error = -1,// wrong protocol version (first 3 bytes)
    msg_type_error = -2,        // msg type is not handled by this protocol
    msg_size_error = -3,        // msg size out of allowed range
    message_corrupted = -4,     // deserialization error
    unexpected_msg_type = -5    // receiving of msg type disabled for this stream
};

/// Protocol errors handler base
struct IErrorHandler {
    virtual ~IErrorHandler() {}

    /// Handles protocol errors
    virtual void on_protocol_error(uint64_t fromStream, ProtocolError error) = 0;

    /// Handles network connection errors
    virtual void on_connection_error(uint64_t fromStream, io::ErrorCode errorCode) = 0;

    /// Per-connection msg type filter fails
    virtual void on_unexpected_msg(uint64_t fromStream, MsgType /*type*/ ) {
        // default impl
        on_protocol_error(fromStream, unexpected_msg_type);
    }
};

class Deserializer;

/// Protocol base
class ProtocolBase {
public:
    ProtocolBase(
        /// 3 bytes for magic # and/or protocol version
        uint8_t protocol_version_0,
        uint8_t protocol_version_1,
        uint8_t protocol_version_2,
        size_t maxMessageTypes,
        IErrorHandler& errorHandler
    ) :
        V0(protocol_version_0), V1(protocol_version_1), V2(protocol_version_2),
        _errorHandler(errorHandler),
        _maxMessageTypes(maxMessageTypes)
    {
        _dispatchTable = new DispatchTableItem[_maxMessageTypes];
    }

    virtual ~ProtocolBase() {
        delete[] _dispatchTable;
    }

    /// Returns header with unknow size for serializer
    MsgHeader get_default_header() {
        return MsgHeader(V0, V1, V2);
    }

    size_t max_message_types() const {
        return _maxMessageTypes;
    }

    /// Called by MsgReader on receiving message header
    bool approve_msg_header(uint64_t fromStream, const MsgHeader& header) {
        ProtocolError error = no_error;

        if (header.V0 != V0 || header.V1 != V1 || header.V2 != V2) {
            error = protocol_version_error;
        } else if (header.type >= _maxMessageTypes) {
            error = msg_type_error;
        } else {
            const DispatchTableItem& i = _dispatchTable[header.type];
            if (!i.callback)
                error = msg_type_error;
            else if (i.minSize > header.size || i.maxSize < header.size)
                error = msg_size_error;
        }

        if (error == no_error) {
            return true;
        }

        _errorHandler.on_protocol_error(fromStream, error);
        return false;
    }

    /// Called by Connection on network errors
    void on_connection_error(uint64_t fromStream, io::ErrorCode errorCode) {
        _errorHandler.on_connection_error(fromStream, errorCode);
    }

    /// Called by msg reader if msg type disabled (by the protocol logic)
    /// for the connection at the moment
    void on_unexpected_msg(uint64_t fromStream, MsgType type) {
        _errorHandler.on_unexpected_msg(fromStream, type);
    }

	void on_corrupt_msg(uint64_t fromStream) {
		_errorHandler.on_protocol_error(fromStream, message_corrupted);
	}

    typedef bool(*OnRawMessage)(
        void* msgHandler,
        IErrorHandler& errorHandler,
        Deserializer& des,
        uint64_t fromStream,
        const void* data,
        size_t size
    );

    /// Called by MsgReader on new message. Returning false means no more reading
    bool on_new_message(uint64_t fromStream, MsgType type, const void* data, size_t size);

	virtual void Decrypt(uint8_t*, uint32_t /*nSize*/) {}
	virtual uint32_t get_MacSize() { return 0; }
	virtual bool VerifyMsg(const uint8_t*, uint32_t /*nSize*/) { return true; } // all together: header, body, MAC

private:
    /// protocol version, all received messages must have these bytes
    uint8_t V0, V1, V2;

protected:
    /// Protocol error handler
    IErrorHandler& _errorHandler;

    /// Deserializer for deriving classes, avoiding code bloat
    Deserializer* _deserializer=0;

    struct DispatchTableItem {
        /// Callback that dispatches msg
        OnRawMessage callback=0;

        /// Message handler object (if handler is member fn)
        void* msgHandler=0;

        /// Min deserialized message size
        uint32_t minSize=0;

        /// Max deserialized message size (against attacks)
        uint32_t maxSize=0;
    };

    size_t _maxMessageTypes;

    /// Raw messages dispatch table for this protocol
    DispatchTableItem* _dispatchTable;
};

} //namespace
