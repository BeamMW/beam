#pragma once
#include "protocol_base.h"

namespace beam {

/// Extracts (serialized, raw data) individual messages from stream, performs header/size validation
class MsgReader {
public:
    /// Ctor sets initial statr (reading_header)
    MsgReader(ProtocolBase& protocol, uint64_t from, size_t defaultSize);

    /// Called from the stream on new data.
    /// Calls the callback whenever a new protocol message is exctracted or on errors
    void new_data_from_stream(const void* data, size_t size);

private:
    /// 2 states of the reader
    enum State { reading_header, reading_message, corrupted };

    /// Called from new_data_from_stream() in loop since more than 1 message could be read at once
    size_t feed_data(const uint8_t* p, size_t sz);

    /// Callbacks
    ProtocolBase& _protocol;

    /// Stream ID for callback
    uint64_t _from;

    /// Initial buffer size
    const size_t _defaultSize;

    /// Bytes left to read before completing header or message
    size_t _bytesLeft;

    /// Current state
    State _state;

    /// Current msg type
    MsgType _type;

    /// Message buffer, grows if needed
    std::vector<uint8_t> _msgBuffer;

    /// Cursor inside the buffer
    uint8_t* _cursor;
};

} //namespace
