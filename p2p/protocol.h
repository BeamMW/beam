#pragma once
#include "protocol_base.h"
#include "msg_serializer.h"

namespace beam {

/// Network<=>App logic message-oriented protocol
template <typename MsgHandler> class Protocol : public ProtocolBase {
public:
    Protocol(
        /// 3 bytes for magic # and/or protocol version
        uint8_t protocol_version_0,
        uint8_t protocol_version_1,
        uint8_t protocol_version_2,
        MsgHandler& handler,
        size_t serializedFragmentsSize
    ) :
        ProtocolBase(protocol_version_0, protocol_version_1, protocol_version_2, handler),
        _ser(serializedFragmentsSize, get_default_header())
    {
        // must static cast - i.e. IMsgHandler must be the 1st base of handler impl
        assert(&handler == static_cast<IMsgHandler*>(&handler));

        // avoid ing code bloat for those included protocol_base.h
        _deserializer = &_des;
    }

    /// Called on protocol dispatch table setup, custom callback
    void add_custom_message_handler(MsgType type, uint32_t minMsgSize, uint32_t maxMsgSize, OnRawMessage callback) {
        DispatchTableItem& i = _dispatchTable[type];
        i.minSize = minMsgSize;
        i.maxSize = maxMsgSize;
        i.callback = callback;
    }

    /// Called on protocol dispatch table setup
    template <
        typename MsgObject,
        bool(MsgHandler::*MessageFn)(uint64_t, const MsgObject&)
    >
    void add_message_handler(MsgType type, uint32_t minMsgSize, uint32_t maxMsgSize) {
        add_custom_message_handler(
            type, minMsgSize, maxMsgSize,
            [](IMsgHandler& handler, Deserializer& des, uint64_t fromStream, const void* data, size_t size) -> bool {
                MsgObject m;
                des.reset(data, size);
                if (!des.deserialize(m) || des.bytes_left() > 0) {
                    handler.on_protocol_error(fromStream, message_corrupted);
                    return false;
                }
                return (static_cast<MsgHandler&>(handler).*MessageFn)(fromStream, m);
            }
        );
    }

    template <typename MsgObject> void serialize(SerializedMsg& out, MsgType type, const MsgObject& obj) {
        _ser.new_message(type);
        _ser & obj;
        _ser.finalize(out);
    }

private:
    Deserializer _des;
    MsgSerializer _ser;
};

} //namespace
