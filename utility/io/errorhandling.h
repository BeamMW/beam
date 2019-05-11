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
#include "utility/expected.h"
#include "libuv.h"
#include <stdexcept>
#include <string>

namespace beam { namespace io {

/// Error codes from libuv + custom error codes
enum ErrorCode {
    EC_OK = 0,
    EC_WRITE_BUFFER_OVERFLOW = UV_ERRNO_MAX - 1,
    EC_FILE_CORRUPTED = UV_ERRNO_MAX - 2,
    EC_SSL_ERROR = UV_ERRNO_MAX - 3,
    EC_HOST_RESOLVED_ERROR = UV_ERRNO_MAX - 4,
#define XX(code, _) EC_ ## code = UV_ ## code,
    UV_ERRNO_MAP(XX)
#undef XX
};

/// Tag struct for returning no errors
struct Ok {
    operator bool() const { return true; }
    bool operator==(Ok) { return true; }
};

/// Result type for functions that may return error codes
using Result = expected<Ok, ErrorCode>;

/// Result returning helper
inline Result make_result(ErrorCode errorCode) { return errorCode ? make_unexpected(errorCode) : Result(); }

/// Returns short error string, e.g. "EINVAL"
const char* error_str(ErrorCode errorCode);

/// Returns more verbose error description
const char* error_descr(ErrorCode errorCode);

/// Formats error code to be shown by exception::what()
std::string format_io_error(const char* _function, const char* _file, int _line, ErrorCode _code);

/// Exception from beam::io
struct Exception : public std::runtime_error {
#ifdef SHOW_CODE_LOCATION
    std::string function;
    std::string file;
    int line;
#endif
    ErrorCode errorCode;

    Exception(const char* _function, const char* _file, int _line, ErrorCode _code) :
        std::runtime_error(format_io_error(_function,_file,_line,_code)),
#ifdef SHOW_CODE_LOCATION
        function(_function),
        file(_file),
        line(_line),
#endif
        errorCode(_code)
    {}
};

#define IO_EXCEPTION(Code) throw io::Exception(__FUNCTION__, __FILE__, __LINE__, Code)
#define IO_EXCEPTION_IF(Code) if (Code != 0) throw io::Exception(__FUNCTION__, __FILE__, __LINE__, Code)

}} //namespaces
