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
#include "utility/io/fragment_writer.h"
#include "utility/io/errorhandling.h"

namespace beam {

/// Line protocol for stratum
class LineReader {
public:
    using OnNewLine = std::function<bool(void* buf, size_t size)>;

    explicit LineReader(
        OnNewLine readCallback, size_t maxLineSize=65536
    ) :
        _maxLineSize(maxLineSize),
        _readCallback(std::move(readCallback))
    {}

    // returns false if line not found within max line size OR read callback returns false
    bool new_data_from_stream(void* data, size_t size) {
        static const char EOL = 10;
        auto n = size;
        auto p = (char*)data;
        while (n > 0) {
            auto q = (char*)memchr(p, EOL, n);
            if (q) {
                ++q;
                size_t fragmentSize = q - p;
                if (!line_found(p, fragmentSize)) {
                    _lineBuffer.clear();
                    return false;
                }
                n -= fragmentSize;
                p = q;
            } else {
                return line_not_found(p, n);
            }
        }
        return true;
    }

private:
    bool line_found(char* p, size_t fragmentSize) {
        size_t lineSize = _lineBuffer.size() + fragmentSize;
        if (lineSize > _maxLineSize) {
            return false;
        }
        void* data = 0;
        if (_lineBuffer.empty()) {
            data = p;
        } else {
            _lineBuffer.append(p, fragmentSize);
            data = _lineBuffer.data();
        }
        bool r = _readCallback(data, lineSize);
        _lineBuffer.clear();
        return r;
    }

    bool line_not_found(char* p, size_t fragmentSize) {
        size_t lineSize = _lineBuffer.size() + fragmentSize;
        if (lineSize > _maxLineSize) {
            return false;
        }
        _lineBuffer.append(p, fragmentSize);
        return true;
    }

    const size_t _maxLineSize;
    std::string _lineBuffer;
    OnNewLine _readCallback;
};

class LineProtocol : public io::FragmentWriter, public LineReader {
public:
    using OnNewLine = LineReader::OnNewLine;
    using OnNewWriteFragment = io::FragmentWriter::OnNewFragment;

    LineProtocol(
        OnNewLine readCallback, const OnNewWriteFragment& writeCallback,
        size_t _outFragmentSize=4096, size_t maxLineSize=65536
    ) :
        io::FragmentWriter(_outFragmentSize, 0, writeCallback),
        LineReader(std::move(readCallback), maxLineSize)
    {}
};

} //namespace
