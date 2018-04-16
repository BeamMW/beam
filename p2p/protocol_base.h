#pragma once
#include "utility/io/buffer.h"
#include <vector>

namespace beam {

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
        static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "Big endian not supported");
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

/// Message handlers base
struct IMsgHandler {
    virtual ~IMsgHandler() {}

    virtual void on_protocol_error(uint64_t fromStream, ProtocolError error) = 0;
    virtual void on_connection_error(uint64_t fromStream, int errorCode) = 0;

/*
    In derived classes, add such functions

    bool on_whatever_1(uint64_t fromStream, const Whatever_1&);
    bool on_whatever_2(uint64_t fromStream, const Whatever_2&);
*/

};

class Deserializer;

/// Protocol base
class ProtocolBase {
public:
    using Message = std::vector<io::SharedBuffer>;

    ProtocolBase(
        /// 3 bytes for magic # and/or protocol version
        uint8_t protocol_version_0,
        uint8_t protocol_version_1,
        uint8_t protocol_version_2,
        IMsgHandler& handler
    ) :
        V0(protocol_version_0), V1(protocol_version_1), V2(protocol_version_2),
        _handler(handler)
    {}

    typedef bool(*OnRawMessage)(IMsgHandler& handler, Deserializer& des, uint64_t fromStream, const void* data, size_t size);

    /// Returns header with unknow size for serializer
    MsgHeader get_default_header() {
        return MsgHeader(V0, V1, V2);
    }

    /// Called by MsgReader on receiving message header
    bool approve_msg_header(uint64_t fromStream, const MsgHeader& header) {
        ProtocolError error = no_error;
        if (header.V0 != V0 || header.V1 != V1 || header.V2 != V2) {
            error = protocol_version_error;
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
        _handler.on_protocol_error(fromStream, error);
        return false;
    }

    /// Called by Connection on network errors
    void on_connection_error(uint64_t fromStream, int errorCode) {
        _handler.on_connection_error(fromStream, errorCode);
    }

    /// Called by MsgReader on new message. Returning false means no more reading
    bool on_new_message(uint64_t fromStream, MsgType type, const void* data, size_t size) {
        OnRawMessage callback = _dispatchTable[type].callback;
        if (!callback) {
            _handler.on_protocol_error(fromStream, msg_type_error);
            return false;
        }
        // TODO assert(_dispatchTable[type].minSize <= size || _dispatchTable[type].maxSize >= size);
        return callback(_handler, *_deserializer, fromStream, data, size);
    }

    // serializes
    //template <typename MsgObject> expected<void, ProtocolError> create_message(Message& out, uint8_t type, const MsgObject& o);

    //void on_new_data(uint64_t fromStream, const void* data, size_t size);

private:
    /// protocol version, all received messages must have these bytes
    uint8_t V0, V1, V2;

protected:
    /// Message handler base
    IMsgHandler& _handler;

    /// Deserializer for deriving classes, avoiding code bloat
    Deserializer* _deserializer=0;

    struct DispatchTableItem {
        uint32_t minSize=0;
        uint32_t maxSize=0;
        OnRawMessage callback=0;
    };

    /// Raw messages dispatch table for this protocol
    DispatchTableItem _dispatchTable[256];
};

} //namespace
