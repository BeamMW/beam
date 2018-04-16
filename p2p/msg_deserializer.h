#pragma once
#include "protocol.h"
#include "utility/io/buffer.h"
#include "utility/serialize_fwd.h"
#include "utility/serialize_streams.h"
#include "utility/yas/binary_iarchive.hpp"
#include "utility/yas/std_types.hpp"
#include <functional>
#include <assert.h>

namespace beam {

class MsgReader {
public:
    enum Error {
        no_error, protocol_version_error, msg_type_error, msg_size_error
    };

    // if callback returns false, then no more reads from the stream
    using Callback = std::function<bool(Error errorCode, protocol::MsgType type, const void* data, size_t size)>;

    MsgReader(size_t defaultSize, const Callback& callback) :
        _defaultSize(defaultSize),
        _bytesLeft(protocol::MsgHeader::SIZE),
        _state(reading_header),
        _type(protocol::MsgType::null),
        _callback(callback)
    {
        assert(_defaultSize >= protocol::MsgHeader::SIZE);
        assert(_callback);
        _msgBuffer.resize(_defaultSize);
        _cursor = _msgBuffer.data();
    }

    void new_data_from_stream(const void* data, size_t size) {
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

private:
    enum State { reading_header, reading_message };

    size_t feed_data(const uint8_t* p, size_t sz) {
        size_t consumed = std::min(sz, _bytesLeft);
        memcpy(_cursor, p, consumed);
        if (_state == reading_header) {
            if (consumed == _bytesLeft) {
                // whole header has been read
                protocol::MsgHeader header(_msgBuffer.data());
                if (!header.verify_protocol_version()) {
                    _callback(protocol_version_error, protocol::MsgType::null, 0, 0);
                    return 0;
                }
                if (!header.verify_type()) {
                    _callback(msg_type_error, protocol::MsgType::null, 0, 0);
                    return 0;
                }
                if (!header.verify_size()) {
                    _callback(msg_size_error, protocol::MsgType::null, 0, 0);
                    return 0;
                }
                // header deserialized successfully
                if (_msgBuffer.size() < header.size) _msgBuffer.resize(header.size);
                _type = header.type;
                _cursor = _msgBuffer.data();
                _bytesLeft = header.size;
                _state = reading_message;
            } else {
                _cursor += consumed;
                _bytesLeft -= consumed;
            }
        } else {
            assert(_state == reading_message);
            if (consumed == _bytesLeft) {
                // whole message has been read
                bool proceed = _callback(no_error, _type, _msgBuffer.data(), _msgBuffer.size());
                if (_msgBuffer.size() > 2*_defaultSize) {
                    {
                        std::vector<uint8_t> newBuffer;
                        _msgBuffer.swap(newBuffer);
                    }
                    // preventing from excessive memory consumption per individual stream
                    _msgBuffer.resize(_defaultSize);
                    _cursor = _msgBuffer.data();
                    _bytesLeft = protocol::MsgHeader::SIZE;
                    _state = reading_header;
                }
                if (!proceed) {
                    // deserialization or valdation error on callback's side
                    return 0;
                }
            } else {
                _cursor += consumed;
                _bytesLeft -= consumed;
            }
        }
        return consumed;
    }

    const size_t _defaultSize;
    size_t _bytesLeft;
    State _state;
    protocol::MsgType _type;
    Callback _callback;
    std::vector<uint8_t> _msgBuffer;
    uint8_t* _cursor;
};

class MsgDeserializer {
public:
    MsgDeserializer() : _ia(_is) {}

    void reset(const void* buf, size_t size) {
        _is.reset(buf, size);
    }

    size_t bytes_left() {
        return _is.bytes_left();
    }

    template <typename T> bool deserialize(T& object) {
        try {
            _ia & object;
        } catch (...) {
            return false;
        }
        return true;
    }

    template <typename T> MsgDeserializer& operator&(T& object) {
        _ia & object;
        return *this;
    }

private:
    using Istream = detail::SerializeIstream;

    Istream _is;
    yas::binary_iarchive<Istream, SERIALIZE_OPTIONS> _ia;
};

} //namespace

