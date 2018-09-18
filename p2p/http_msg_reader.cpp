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

#include "http_msg_reader.h"
#include "picohttpparser/picohttpparser.h"
#include <map>
#include <algorithm>
#include <assert.h>

namespace beam {

namespace {

static const size_t MAX_HEADERS_NUMBER = 100;
static const size_t MAX_HEADERS_BUFSIZE = 4096;

inline bool equal_ci(const char* a, const char* b, size_t sz) {
    const char* e = a + sz;
    for (; a !=e; ++a, ++b) {
        if (*a != tolower(*b)) return false;
    }
    return true;
}

struct ParserStuff {
    bool ready;
    int minor_http_version;
    const char* method;
    size_t method_len;
    const char* path;
    size_t path_len;
    size_t buf_len;
    size_t prev_buf_len;
    mutable size_t num_headers;
    mutable struct phr_header headers[MAX_HEADERS_NUMBER];
    char headers_buffer[MAX_HEADERS_BUFSIZE];

    void reset_headers() {
        ready = false;
        minor_http_version = -1;
        method = 0;
        method_len = 0;
        path = 0;
        path_len = 0;
        buf_len = MAX_HEADERS_BUFSIZE;
        prev_buf_len = 0;
        num_headers = MAX_HEADERS_NUMBER;
    }

    std::string find_header(const std::string& lowerCaseHeader) const {
        std::string value;
        size_t sz = lowerCaseHeader.size();
        for (size_t i = 0; i != num_headers; ++i) {
            if (headers[i].name_len != sz) continue;
            if (!equal_ci(lowerCaseHeader.c_str(), headers[i].name, sz)) continue;
            value.assign(headers[i].value, headers[i].value_len);
            if (i != num_headers-1) {
                headers[i] = headers[num_headers-1];
            }
            --num_headers;
        }
        return value;
    }
};

static const std::string dummyStr;

} //namespace

class HttpMessageImpl : public HttpMessage, public ParserStuff {
public:
    HttpMessageImpl() {
        reset_headers();
    }

    ~HttpMessageImpl() {}

private:
    const std::string& get_method() const override {
        if (!ready) return dummyStr;
        if (_methodCached.empty()) {
            _methodCached.assign(method, method_len);
        }
        return _methodCached;
    }

    const std::string& get_path() const override {
        if (!ready) return dummyStr;
        if (_pathCached.empty()) {
            _pathCached.assign(path, path_len);
        }
        return _pathCached;
    }

    const std::string& get_header(const std::string& headerName) const override {
        if (!ready || headerName.empty()) return dummyStr;
        std::string lowerCaseHeader(headerName);
        std::transform(lowerCaseHeader.begin(), lowerCaseHeader.end(), lowerCaseHeader.begin(), tolower);
        std::map<std::string, std::string>::iterator it = _headersCached.find(lowerCaseHeader);
        if (it == _headersCached.end()) {
            it = _headersCached.insert({ lowerCaseHeader, find_header(lowerCaseHeader) }).first;
        }
        return it->second;
    }

    const void* get_body(size_t& size) const override {
        if (!ready) {
            size = 0;
            return 0;
        }
        size = _body.size();
        return _body.data();
    }

public:
    std::vector<uint8_t> _body;

    /// Cursor inside body
    uint8_t* _cursor=0;

    void reset(size_t bodySizeThreshold) {
        reset_headers();
        if (_body.size() > bodySizeThreshold) {
            std::vector<uint8_t> newBody;
            std::swap(_body, newBody);
        }
        _cursor = 0;
        _methodCached.clear();
        _pathCached.clear();
        _headersCached.clear();
    }

private:
    mutable std::string _methodCached;
    mutable std::string _pathCached;
    mutable std::map<std::string, std::string> _headersCached;
};

HttpMsgReader::HttpMsgReader(
    HttpMsgReader::Mode mode,
    uint64_t streamId,
    Callback callback,
    size_t maxBodySize,
    size_t bodySizeThreshold
) :
    _callback(std::move(callback)),
    _streamId(streamId),
    _maxBodySize(maxBodySize),
    _bodySizeThreshold(bodySizeThreshold),
    _bytesLeft(0),
    _state(reading_header),
    _mode(mode),
    _msg(std::make_unique<HttpMessageImpl>())
{
    assert(_callback);
}

void HttpMsgReader::new_data_from_stream(io::ErrorCode connectionStatus, const void* data, size_t size) {
    if (connectionStatus != io::EC_OK) {
        _callback(_streamId, Message(connectionStatus));
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
            // at this moment, the *this* may be deleted
            return;
        }
        assert(consumed <= sz);
        sz -= consumed;
        p += consumed;
    }
}

size_t HttpMsgReader::feed_data(const uint8_t* p, size_t sz) {
    size_t consumed = std::min(sz, _bytesLeft);

    /*
    memcpy(_cursor, p, consumed);
    if (_state == reading_header) {
        if (consumed == _bytesLeft) {
            // whole header has been read
            MsgHeader header(_msgBuffer.data());
            if (!_protocol.approve_msg_header(_streamId, header)) {
                // at this moment, the *this* may be deleted
                return 0;
            }

            if (!_expectedMsgTypes.test(header.type)) {
                _protocol.on_unexpected_msg(_streamId, header.type);
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
            if (!_protocol.on_new_message(_streamId, _type, _msgBuffer.data(), _msgBuffer.size())) {
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
    */
    return consumed;
}

} //namespace
