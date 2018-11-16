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

#include "msg_reader.h"
#include <assert.h>
#include <algorithm>

namespace beam {

MsgReader::MsgReader(ProtocolBase& protocol, uint64_t streamId, size_t defaultSize) :
    _protocol(protocol),
    _streamId(streamId),
    _defaultSize(defaultSize),
    _bytesLeft(MsgHeader::SIZE),
    _state(reading_header)
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

void MsgReader::reset() {
    _bytesLeft = MsgHeader::SIZE;
    _state = reading_header;
    _cursor = _msgBuffer.data();
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

bool MsgReader::new_data_from_stream(io::ErrorCode connectionStatus, const void* data, size_t size) {
    if (connectionStatus != 0) {
        _protocol.on_connection_error(_streamId, connectionStatus);
        return false;
    }

    if (!data || !size) {
        return true;
    }

	std::shared_ptr<bool> pAlive(_pAlive);
	volatile const bool& bAlive = *pAlive;

    const uint8_t* p = (const uint8_t*)data;
    size_t sz = size;

	while (sz >= _bytesLeft)
	{
		memcpy(_cursor, p, _bytesLeft);
		_protocol.Decrypt(_cursor, (uint32_t) _bytesLeft); // decrypt as much as we expect, no more (because cipher may change)

		sz -= _bytesLeft;
		p += _bytesLeft;

		MsgHeader header(_msgBuffer.data());

		if (_state == reading_header)
		{
			// header has just been read
			if (!_protocol.approve_msg_header(_streamId, header))
				// at this moment, the *this* may be deleted
				return false;

			if (!bAlive)
				return false;

			if (!_expectedMsgTypes.test(header.type)) {
				_protocol.on_unexpected_msg(_streamId, header.type);
				// at this moment, the *this* may be deleted
				return false;
			}

			if (!bAlive)
				return false;

			// header deserialized successfully
			_bytesLeft = header.size;
			_msgBuffer.resize(MsgHeader::SIZE + _bytesLeft);
			_cursor = _msgBuffer.data() + MsgHeader::SIZE;

			_state = reading_message;

		}
		else
		{
			// whole message has been read
			if (!_protocol.VerifyMsg(_msgBuffer.data(), static_cast<uint32_t>(_msgBuffer.size())))
			{
				_protocol.on_corrupt_msg(_streamId);
				return false;
			}

            if (!_protocol.on_new_message(_streamId, header.type, _msgBuffer.data() + MsgHeader::SIZE, header.size - _protocol.get_MacSize())) {
                // at this moment, the *this* may be deleted
                if (bAlive) {
                    reset();
                }
                return false;
            }

			if (!bAlive)
				return false;

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

			_cursor = _msgBuffer.data();
		}
	}

	if (sz)
	{
		memcpy(_cursor, p, sz);
		_protocol.Decrypt(_cursor, (uint32_t) sz);

		_cursor += sz;
		_bytesLeft -= sz;
	}

	return true;
}


} //namespace
