#include "msg_reader.h"
#include <assert.h>
#include <algorithm>

namespace beam {

MsgReader::MsgReader(ProtocolBase& protocol, uint64_t from, size_t defaultSize) :
    _protocol(protocol),
    _from(from),
    _defaultSize(defaultSize),
    _bytesLeft(MsgHeader::SIZE),
    _state(reading_header),
    _type(0)
{
    assert(_defaultSize >= MsgHeader::SIZE);
    _msgBuffer.resize(_defaultSize);
    _cursor = _msgBuffer.data();
}

void MsgReader::new_data_from_stream(const void* data, size_t size) {
    const uint8_t* p = (const uint8_t*)data;
    size_t sz = size;
    size_t consumed = 0;
    while (sz > 0) {
        consumed = feed_data(p, sz);
        if (consumed == 0) {
            // error occured, no more reads from this stream
            // at this moment, the *this* may be deleted
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
            if (!_protocol.approve_msg_header(_from, header)) {
                // at this moment, the *this* may be deleted
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
    } else {
        if (consumed == _bytesLeft) {
            // whole message has been read
            if (!_protocol.on_new_message(_from, _type, _msgBuffer.data(), _msgBuffer.size())) {
                // at this moment, the *this* may be deleted
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
    }
    return consumed;
}

} //namespace
