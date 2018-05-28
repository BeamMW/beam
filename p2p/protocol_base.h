#pragma once
#include "utility/io/buffer.h"
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
    MsgHeader(uint8_t _v0, uint8_t _v1, uint8_t _v2, MsgType _type=0, size_t _size=0) :
        V0(_v0),
        V1(_v1),
        V2(_v2),
        type(_type),
        size(_size)
    {}

    /// Reuse ctor
    void reset(MsgType _type = 0, size_t _size=0) {
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

/// Errors occured during deserialization
enum ProtocolError {
    no_error = 0,
    protocol_version_error = -1,
    msg_type_error = -2,
    msg_size_error = -3,
    message_corrupted = -4
};

/// Protocol errors handler base
struct IErrorHandler {
    virtual ~IErrorHandler() {}

    /// Handles protocol errors
    virtual void on_protocol_error(uint64_t fromStream, ProtocolError error) = 0;

    /// Handles network connection errors
    virtual void on_connection_error(uint64_t fromStream, int errorCode) = 0;
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
    void on_connection_error(uint64_t fromStream, int errorCode) {
        _errorHandler.on_connection_error(fromStream, errorCode);
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
    bool on_new_message(uint64_t fromStream, MsgType type, const void* data, size_t size) {
        OnRawMessage callback = _dispatchTable[type].callback;
        if (!callback) {
            _errorHandler.on_protocol_error(fromStream, msg_type_error);
            return false;
        }
        return callback(_dispatchTable[type].msgHandler, _errorHandler, *_deserializer, fromStream, data, size);
    }

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
