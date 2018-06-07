#include "msg_reader.h"
#include <assert.h>
#include <algorithm>

namespace beam {

MsgReader::MsgReader(ProtocolBase& protocol, uint64_t streamId, size_t defaultSize) :
    _protocol(protocol),
    _streamId(streamId),
    _defaultSize(defaultSize),
    _bytesLeft(MsgHeader::SIZE),
    _state(reading_header),
    _type(0)
{
    assert(_defaultSize >= MsgHeader::SIZE);
    _msgBuffer.resize(_defaultSize);
    _cursor = _msgBuffer.data();

    // by default, all message types are allowed
    enable_all_msg_types();
}

void MsgReader::change_id(uint64_t newStreamId) {
    _streamId = newStreamId;
}

void MsgReader::enable_msg_type(MsgType type) {
    _expectedMsgTypes.set(type);
}

void MsgReader::enable_all_msg_types() {
    _expectedMsgTypes.set();
}

void MsgReader::disable_msg_type(MsgType type) {
    _expectedMsgTypes.reset(type);
}

void MsgReader::disable_all_msg_types() {
    _expectedMsgTypes.reset();
}

void MsgReader::new_data_from_stream(io::ErrorCode connectionStatus, const void* data, size_t size) {
    if (connectionStatus != 0) {
        _state = corrupted;
        _protocol.on_connection_error(_streamId, connectionStatus);
        return;
    }

    if (!data || !size) {
        return;
    }

    const uint8_t* p = (const uint8_t*)data;
    size_t sz = size;
    size_t consumed = 0;
    while (sz > 0) {
        consumed = feed_data(p, sz);
        if (consumed == 0) {
            // error occured, no more reads from this stream
            return;
        }
        assert(consumed <= sz);
        sz -= consumed;
        p += consumed;
    }
}

size_t MsgReader::feed_data(const uint8_t* p, size_t sz) {
    size_t consumed = std::min(sz, _bytesLeft);
    memcpy(_cursor, p, consumed);
    if (_state == reading_header) {
        if (consumed == _bytesLeft) {
            // whole header has been read
            MsgHeader header(_msgBuffer.data());
            if (!_protocol.approve_msg_header(_streamId, header)) {
                _state = corrupted;
                return 0;
            }

            if (!_expectedMsgTypes.test(header.type)) {
                _protocol.on_unexpected_msg(_streamId, header.type);
                _state = corrupted;
                return 0;
            }

            // header deserialized successfully
            _msgBuffer.resize(header.size);
            _type = header.type;
            _cursor = _msgBuffer.data();
            _bytesLeft = header.size;
            _state = reading_message;
        } else {
            _cursor += consumed;
            _bytesLeft -= consumed;
        }
    } else if (_state == reading_message) {
        if (consumed == _bytesLeft) {
            // whole message has been read
            if (!_protocol.on_new_message(_streamId, _type, _msgBuffer.data(), _msgBuffer.size())) {
                _state = corrupted;
                return 0;
            }
            if (_msgBuffer.size() > 2*_defaultSize) {
                {
                    std::vector<uint8_t> newBuffer;
                    _msgBuffer.swap(newBuffer);
                }
                // preventing from excessive memory consumption per individual stream
                _msgBuffer.resize(_defaultSize);
            }
            _cursor = _msgBuffer.data();
            _bytesLeft = MsgHeader::SIZE;
            _state = reading_header;
        } else {
            _cursor += consumed;
            _bytesLeft -= consumed;
        }
    } else {
        return 0;
    }
    return consumed;
}

} //namespace
