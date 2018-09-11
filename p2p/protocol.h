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
#include "protocol_base.h"
#include "msg_serializer.h"
#include <stdexcept>

namespace beam {

/// Network<=>App logic message-oriented protocol
class Protocol : public ProtocolBase {
public:
    Protocol(
        /// 3 bytes for magic # and/or protocol version
        uint8_t protocol_version_0,
        uint8_t protocol_version_1,
        uint8_t protocol_version_2,
        size_t maxMessageTypes,
        IErrorHandler& errorHandler,
        size_t serializedFragmentsSize
    ) :
        ProtocolBase(protocol_version_0, protocol_version_1, protocol_version_2, maxMessageTypes, errorHandler),
        _ser(serializedFragmentsSize, get_default_header())
    {
        // avoiding code bloat for those included protocol_base.h
        _deserializer = &_des;
    }

    /// Called on protocol dispatch table setup, custom callback
    void add_custom_message_handler(MsgType type, void* msgHandler, uint32_t minMsgSize, uint32_t maxMsgSize, OnRawMessage callback) {
        if (type >= _maxMessageTypes) {
            throw std::runtime_error("protocol: message type out of range");
        }
        DispatchTableItem& i = _dispatchTable[type];
        if (i.callback) {
            throw std::runtime_error("protocol: message handler already set");
        }
        i.callback = callback;
        i.msgHandler = msgHandler;
        i.minSize = minMsgSize;
        i.maxSize = maxMsgSize;
    }

    /// Called on protocol dispatch table setup
    template <
        typename MsgHandler,
        typename MsgObject,
        bool(MsgHandler::*MessageFn)(uint64_t, MsgObject&&)
    >
    void add_message_handler(MsgType type, MsgHandler* msgHandler, uint32_t minMsgSize, uint32_t maxMsgSize) {
        add_custom_message_handler(
            type, msgHandler, minMsgSize, maxMsgSize,
            [](void* msgHandler, IErrorHandler& errorHandler, Deserializer& des, uint64_t fromStream, const void* data, size_t size) -> bool {
                MsgObject m;
                des.reset(data, size);
                if (!des.deserialize(m) || des.bytes_left() > 0) {
                    errorHandler.on_protocol_error(fromStream, message_corrupted);
                    return false;
                }
                return (static_cast<MsgHandler*>(msgHandler)->*MessageFn)(fromStream, std::move(m));
            }
        );
    }

    /// Called on protocol dispatch table setup, free function as message handler
    template <
        typename MsgObject,
        bool(*MessageFn)(uint64_t, MsgObject&&)
    >
    void add_message_handler(MsgType type, uint32_t minMsgSize, uint32_t maxMsgSize) {
        add_custom_message_handler(
            type, 0, minMsgSize, maxMsgSize,
            [](void*, IErrorHandler& errorHandler, Deserializer& des, uint64_t fromStream, const void* data, size_t size) -> bool {
                MsgObject m;
                des.reset(data, size);
                if (!des.deserialize(m) || des.bytes_left() > 0) {
                    errorHandler.on_protocol_error(fromStream, message_corrupted);
                    return false;
                }
                return MessageFn(fromStream, std::move(m));
            }
        );
    }

    /// If externalTailSize > 0 then serialized msg must be followed by raw buffer of thet size
    template <typename MsgObject> void serialize(SerializedMsg& out, MsgType type, const MsgObject& obj, size_t externalTailSize=0) {
		serializeNoFinalize(out, type, obj);
        if (externalTailSize > 0) _ser & externalTailSize;
        _ser.finalize(out, externalTailSize);
    }

	template <typename MsgObject> MsgSerializer& serializeNoFinalize(SerializedMsg& out, MsgType type, const MsgObject& obj) {
		_ser.new_message(type);
		_ser & obj;
		return _ser;
	}

	/// If externalTailSize > 0 then serialized msg must be followed by raw buffer of thet size
    template <typename MsgObject> io::SharedBuffer serialize(
        MsgType type, const MsgObject& obj, bool makeUnique, size_t externalTailSize=0
    ) {
        SerializedMsg fragments;
        serialize(fragments, type, obj, externalTailSize);
        return io::normalize(fragments, makeUnique);
    }

private:
    Deserializer _des;
    MsgSerializer _ser;
};

} //namespace
