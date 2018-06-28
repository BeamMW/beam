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
	_pAlive.reset(new bool);
	*_pAlive = true;

    assert(_defaultSize >= MsgHeader::SIZE);
    _msgBuffer.resize(_defaultSize);
    _cursor = _msgBuffer.data();

    // by default, all message types are allowed
    enable_all_msg_types();
}

MsgReader::~MsgReader()
{
	if (_pAlive)
		*_pAlive = false;
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
        _protocol.on_connection_error(_streamId, connectionStatus);
        return;
    }

    if (!data || !size) {
        return;
    }

	std::shared_ptr<bool> pAlive(_pAlive);

    const uint8_t* p = (const uint8_t*)data;
    size_t sz = size;
    size_t consumed = 0;
    while ((sz > 0) && *pAlive) {
        consumed = feed_data(p, sz, *pAlive);
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

size_t MsgReader::feed_data(const uint8_t* p, size_t sz, const volatile bool& bAlive)
{
    size_t consumed = std::min(sz, _bytesLeft);
    memcpy(_cursor, p, consumed);
	_protocol.Decrypt(_cursor, (uint32_t) consumed); // decrypt as much as we expect, no more (because cipher may change)

	if (consumed == _bytesLeft)
	{
		if (_state == reading_header)
		{
			// whole header has been read
			MsgHeader header(_msgBuffer.data());
			if (!_protocol.approve_msg_header(_streamId, header)) {
				// at this moment, the *this* may be deleted
				return 0;
			}

			if (!bAlive)
				return 0;

			if (!_expectedMsgTypes.test(header.type)) {
				_protocol.on_unexpected_msg(_streamId, header.type);
				// at this moment, the *this* may be deleted
				return 0;
			}

			if (!bAlive)
				return 0;

			// header deserialized successfully
			_msgBuffer.resize(header.size);
			_type = header.type;
			_bytesLeft = header.size;
			_state = reading_message;
		}
		else
		{
			// whole message has been read
			if (!_protocol.on_new_message(_streamId, _type, _msgBuffer.data(), _msgBuffer.size())) {
				// at this moment, the *this* may be deleted
				return 0;
			}

			if (!bAlive)
				return 0;

			if (_msgBuffer.size() > 2 * _defaultSize) {
				{
					std::vector<uint8_t> newBuffer;
					_msgBuffer.swap(newBuffer);
				}
				// preventing from excessive memory consumption per individual stream
				_msgBuffer.resize(_defaultSize);
			}
			_bytesLeft = MsgHeader::SIZE;
			_state = reading_header;
		}

		_cursor = _msgBuffer.data();
	}
	else
	{
		_cursor += consumed;
		_bytesLeft -= consumed;
	}

    return consumed;
}

} //namespace
