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
#include "utility/io/errorhandling.h"
#include <functional>
#include <string_view>
#include <memory>
#include <map>
#include <set>

namespace beam {

/// Http url parsed into string_views
struct HttpUrl {
    static const size_t MAX_PATH_ELEMENTS = 10;

    int dir=-1;
    unsigned nPathElements = 0;
    std::string_view path[MAX_PATH_ELEMENTS];
    std::map<std::string_view, std::string_view> args;
    std::string_view fragment;

    void reset() {
        dir = -1;
        nPathElements = 0;
        args.clear();
    }

    int64_t get_int_arg(const std::string_view& name, int64_t defValue) const;

    /// Parses path string, dirs contain 1st words in path. If dir is not found, all the rest is not parsed
    bool parse(const std::string& url, const std::map<std::string_view, int>& dirs);
};

/// Http message passed through callbacks
class HttpMessage {
public:
    virtual ~HttpMessage() = default;
    virtual const std::string& get_method() const = 0;
    virtual const std::string& get_path() const = 0;
    virtual const std::string& get_header(const std::string& headerName) const = 0;
    virtual const void* get_body(size_t& size) const = 0;
};

/// Extracts individual http messages from stream, performs header/size validation
class HttpMsgReader {
public:
    enum What { nothing, http_message, connection_error, message_corrupted, message_too_long };

    struct Message {
        What what;
        union {
            const HttpMessage* msg;
            io::ErrorCode connectionError;
        };

        std::string error_str() const;

        Message() : what(nothing), msg(0) {}
        Message(const HttpMessage* m) : what(http_message), msg(m) {}
        Message(io::ErrorCode e) : what(connection_error), connectionError(e) {}
        Message(What w) : what(w), msg(0) {}
    };

    using Callback = std::function<bool(uint64_t streamId, const Message& msg)>;

    enum Mode { server, client };

    /// Ctor sets initial statr (reading_header)
    HttpMsgReader(Mode mode, uint64_t streamId, Callback callback, size_t maxBodySize, size_t bodySizeThreshold);

    ~HttpMsgReader();
    HttpMsgReader(const HttpMsgReader&) = delete;
    HttpMsgReader& operator=(const HttpMsgReader&) = delete;

    uint64_t id() const { return _streamId; }
    void change_id(uint64_t newStreamId) { _streamId = newStreamId; }

    /// Called from the stream on new data.
    /// Calls the callback whenever a new message is exctracted or on errors
    bool new_data_from_stream(io::ErrorCode connectionStatus, const void* data, size_t size);

    /// Resets to initial state
    void reset();

private:
    size_t feed_header(const uint8_t* p, size_t sz);
    size_t feed_body(const uint8_t* p, size_t sz);

    /// 2 states of the reader
    enum State { reading_header, reading_body };

    /// callback
    Callback _callback;

    /// Stream ID for callback
    uint64_t _streamId;

    /// Body buffer max allowed size
    const size_t _maxBodySize;

    /// Body buffer size threshold
    const size_t _bodySizeThreshold;

    /// Current state
    State _state;

    /// Server or client
    Mode _mode;

    /// Message being parsed
    class HttpMessageImpl* _msg;
};

} //namespace
