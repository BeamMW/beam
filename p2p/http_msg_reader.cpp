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
#include <vector>
#include <algorithm>
#include <string.h>
#include <assert.h>

#include "utility/logger.h"

namespace beam {

using std::string_view;

namespace {

int find_dir(const std::map<string_view, int>& dirs, const string_view& str) {
    int ret = -1;
    auto it = dirs.find(str);
    if (it != dirs.end()) ret = it->second;
    return ret;
}

void split(string_view& a, char delim, string_view& b) {
    auto pos = a.find(delim, 0);
    if (pos == string_view::npos) {
        b = string_view("");
    } else {
        b = a.substr(pos + 1);
        a = a.substr(0, pos);
    }
}

} //namespace

bool HttpUrl::parse(const std::string& url, const std::map<std::string_view, int>& dirs) {
    reset();
    if (url.empty() || url[0] != '/') return false;
    string_view s(url.c_str() + 1, url.size() - 1);
    if (s.empty()) {
        dir = find_dir(dirs, "");
        return (dir >= 0);
    }

    split(s, '#', fragment);
    string_view query;
    split(s, '?', query);

    string_view tail;
    split(s, '/', tail);
    dir = find_dir(dirs, s);
    if (dir < 0) {
        return false;
    }
    for (unsigned i=0; i<MAX_PATH_ELEMENTS; ++i) {
        s = tail;
        split(s, '/', tail);
        if (s.empty())
            break;
        path[i] = s;
        ++nPathElements;
        if (tail.empty())
            break;
    }
    if (!tail.empty())
        return false;

    string_view value;
    while (!query.empty()) {
        split(query, '&', tail);
        split(query, '=', value);
        if (!query.empty()) {
            args[query] = value;
        }
        query = tail;
    }

    return true;
}

int64_t HttpUrl::get_int_arg(const std::string_view& name, int64_t defValue) const {
    auto it = args.find(name);
    if (it == args.end()) return defValue;
    const auto& val = it->second;
    if (val.empty()) return defValue;
    char* e = 0;
    int64_t ret = strtol(val.data(), &e, 10);
    if (size_t(e - val.data()) != val.size()) return defValue;
    return ret;
}

std::string HttpMsgReader::Message::error_str() const {
    switch (what) {
        case HttpMsgReader::connection_error:
            return io::error_descr(connectionError);
        case HttpMsgReader::message_corrupted:
            return "message corrupted";
        case HttpMsgReader::message_too_long:
            return "message too long";
        default:
            break;
    }
    return "ok";
}

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

struct HeadersParserStuff {
    enum State { incompleted, request_parsed, response_parsed };

    State headers_state;
    int minor_http_version;
    int response_status;
    const char* method;
    size_t method_len;
    const char* path;
    size_t path_len;
    const char* response_msg;
    size_t response_msg_len;
    size_t headers_cursor;
    mutable size_t num_headers;
    mutable struct phr_header headers[MAX_HEADERS_NUMBER];
    char headers_buffer[MAX_HEADERS_BUFSIZE];

    void reset_headers() {
        headers_state = incompleted;
        minor_http_version = -1;
        response_status = -1;
        method = 0;
        method_len = 0;
        path = 0;
        path_len = 0;
        response_msg = 0;
        response_msg_len = 0;
        headers_cursor = 0;
        num_headers = MAX_HEADERS_NUMBER;
    }

    std::string find_header(const std::string& lowerCaseHeader) const {
        // TODO multiline headers not supported yet

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
            break;
        }
        return value;
    }

    // returns true iff parsing completed
    bool parse_headers(bool parseResponse, const void* p, size_t sz, size_t& consumed, HttpMsgReader::What& error) {
        int result=0;
        error = HttpMsgReader::nothing;
        consumed = 0;
        if (sz == 0) return false;

        size_t maxBytes = MAX_HEADERS_BUFSIZE - headers_cursor;
        if (sz < maxBytes) maxBytes = sz;
        memcpy(headers_buffer + headers_cursor, p, maxBytes);

        num_headers = MAX_HEADERS_NUMBER;
        if (parseResponse) {
            result = phr_parse_response(headers_buffer, headers_cursor + maxBytes,
                                        &minor_http_version, &response_status,
                                        &response_msg, &response_msg_len,
                                        headers, &num_headers,
                                        headers_cursor );
        } else {
             result = phr_parse_request(headers_buffer, headers_cursor + maxBytes,
                                        &method, &method_len,
                                        &path, &path_len,
                                        &minor_http_version,
                                        headers, &num_headers, headers_cursor);
        }

        if (result >= 0) {
            size_t totalLength = size_t(result);
            assert(totalLength >= headers_cursor);
            consumed = totalLength - headers_cursor;
            headers_state = parseResponse ? response_parsed : request_parsed;
            return true;
        }

        if (result == -1) {
            //LOG_DEBUG() << "Corrupted " << std::string(headers_buffer, headers_cursor + maxBytes);
            error = HttpMsgReader::message_corrupted;
            return false;
        }

        assert(result == -2);

        headers_cursor += maxBytes;
        if ( headers_cursor == MAX_HEADERS_BUFSIZE) {
            error = HttpMsgReader::message_too_long;
        }

        //LOG_DEBUG() << "incompleted: " << std::string(headers_buffer, headers_cursor);

        consumed = maxBytes;
        return false;
    }
};

static const std::string dummyStr;

} //namespace

class HttpMessageImpl : public HttpMessage, public HeadersParserStuff {
public:
    HttpMessageImpl() {
        reset_headers();
    }

    ~HttpMessageImpl() {}

private:
    const std::string& get_method() const override {
        if (headers_state != request_parsed) return dummyStr;
        if (_methodCached.empty()) {
            _methodCached.assign(method, method_len);
        }
        return _methodCached;
    }

    const std::string& get_path() const override {
        if (headers_state != request_parsed) return dummyStr;
        if (_pathCached.empty()) {
            _pathCached.assign(path, path_len);
        }
        return _pathCached;
    }

    const std::string& get_header(const std::string& headerName) const override {
        if (headers_state == incompleted || headerName.empty()) return dummyStr;
        std::string lowerCaseHeader(headerName);
        std::transform(lowerCaseHeader.begin(), lowerCaseHeader.end(), lowerCaseHeader.begin(), [](char c)->char { return (char)tolower(c);} );
        std::map<std::string, std::string>::iterator it = _headersCached.find(lowerCaseHeader);
        if (it == _headersCached.end()) {
            it = _headersCached.insert({ lowerCaseHeader, find_header(lowerCaseHeader) }).first;
        }
        return it->second;
    }

    const void* get_body(size_t& size) const override {
        if (_body.empty() || headers_state == incompleted || _bodyCursor != _body.size()) {
            size = 0;
            return 0;
        }
        size = _bodyCursor;
        return _body.data();
    }

public:
    std::vector<uint8_t> _body;
    size_t _bodyCursor=0;

    void reset(size_t bodySizeThreshold) {
        reset_headers();
        if (_body.size() > bodySizeThreshold) {
            std::vector<uint8_t> newBody;
            std::swap(_body, newBody);
        } else {
            _body.clear();
        }
        _bodyCursor = 0;
        _methodCached.clear();
        _pathCached.clear();
        _headersCached.clear();
    }

    size_t parse_content_length(size_t maxLength, HttpMsgReader::What& error) {
        error = HttpMsgReader::nothing;
        size_t ret = 0;
        if (headers_state != incompleted) {
            static const std::string contentLength("content-length");
            std::string lenStr = find_header(contentLength);
            if (!lenStr.empty()) {
                char* end = 0;
                ret = strtoul(lenStr.c_str(), &end, 10);
                if (end != lenStr.c_str() + lenStr.size()) {
                    error = HttpMsgReader::message_corrupted;
                    return size_t(-1);
                }
                if (ret > maxLength) {
                    error = HttpMsgReader::message_too_long;
                    return size_t(-1);
                }
                _body.resize(ret);
                _bodyCursor = 0;
            }
        }
        return ret;
    }

    size_t feed_body(const uint8_t* p, size_t sz, bool& completed) {
        assert(_bodyCursor <= _body.size());
        size_t maxBytes = _body.size() - _bodyCursor;
        if (sz < maxBytes) {
            maxBytes = sz;
            completed = false;
        } else {
            completed = true;
        }
        if (maxBytes > 0) {
            memcpy(_body.data() + _bodyCursor, p, maxBytes);
            _bodyCursor += maxBytes;
        }
        return maxBytes;
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
    _state(reading_header),
    _mode(mode),
    _msg(new HttpMessageImpl())
{
    assert(_callback);
}

HttpMsgReader::~HttpMsgReader() {
    delete _msg;
}

bool HttpMsgReader::new_data_from_stream(io::ErrorCode connectionStatus, const void* data, size_t size) {
    if (connectionStatus != io::EC_OK) {
        return _callback(_streamId, Message(connectionStatus));
    }

    if (!data || !size) {
        return true;
    }

    const auto* p = (const uint8_t*)data;
    size_t sz = size;
    size_t consumed = 0;
    while (sz > 0) {
        consumed = _state == reading_header ? feed_header(p, sz) : feed_body(p, sz);
        if (consumed == 0) {
            // error occured, no more reads from this stream
            // at this moment, the *this* may be deleted
            return false;
        }
        assert(consumed <= sz);
        sz -= consumed;
        p += consumed;
    }

    return true;
}

size_t HttpMsgReader::feed_header(const uint8_t* p, size_t sz) {
    size_t consumed = 0;
    What error = nothing;
    bool headers_completed = _msg->parse_headers(_mode == client, p, sz, consumed, error);
    if (headers_completed) {
        size_t contentLength = _msg->parse_content_length(_maxBodySize, error);
        if (contentLength == 0) {
            // message w/o body, completed
            bool proceed = _callback(_streamId, Message(_msg));
            if (proceed) {
                _msg->reset(_bodySizeThreshold);
                return consumed;
            } else {
                // the object may be deleted here
                return 0;
            }
        } else if (error == nothing) {
            _state = reading_body;
        } else {
            _callback(_streamId, Message(error));
            // the object may be deleted here
            return 0;
        }
    } else if (error != nothing) {
        _callback(_streamId, Message(error));
        // the object may be deleted here
        return 0;
    }
    return consumed;
}

size_t HttpMsgReader::feed_body(const uint8_t* p, size_t sz) {
    bool completed = false;
    size_t consumed = _msg->feed_body(p, sz, completed);
    if (completed) {
        _state = reading_header;
        bool proceed = _callback(_streamId, Message(_msg));
        if (proceed) {
            _msg->reset(_bodySizeThreshold);
            _state = reading_header;
        } else {
            // the object may be deleted here
            return 0;
        }
    }
    return consumed;
}

void HttpMsgReader::reset() {
    _msg->reset(_bodySizeThreshold);
    _state = reading_header;
}

} //namespace
