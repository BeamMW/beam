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

#include "logger_checkpoints.h"
#include "helpers.h"
#include "common.h"
#include <boost/filesystem.hpp>
#include <boost/iostreams/device/back_inserter.hpp>

#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wunknown-warning-option"
#  pragma clang diagnostic ignored "-Wtautological-constant-compare"
#endif

#include <boost/iostreams/filtering_stream.hpp>

#if defined(__clang__)
#  pragma clang diagnostic pop
#endif

#include <stdexcept>
#include <iostream>
#include <fstream>
#include <mutex>
#include <algorithm>

namespace beam {

using namespace std;

Logger* Logger::g_logger = 0;

class LoggerImpl : public Logger {
protected:
    mutex _mutex;
    static const size_t MAX_HEADER_SIZE = 256;
    static const size_t MAX_TIMESTAMP_SIZE = 80;

    FILE* _sink;
    int _minLevel;
    int _flushLevel;
    LogMessageHeaderFormatter _headerFormatter = def_header_formatter;
    std::string _timeFormat;
    bool _printMilliseconds;

    LoggerImpl(FILE* sink, int minLevel, int flushLevel) :
        _sink(sink),
        _minLevel(minLevel),
        _flushLevel(flushLevel),
        _headerFormatter(def_header_formatter),
        _timeFormat("%Y-%m-%d.%T"),
        _printMilliseconds(true)
    {
        if (minLevel <= 0) throw runtime_error("logger: minimal level out of range");
    }

    virtual ~LoggerImpl() {
        if (this == g_logger) {
            g_logger = 0;
        }
    }

    void set_header_formatter(LogMessageHeaderFormatter formatter) override {
        if (formatter) _headerFormatter = formatter;
    }

    void set_time_format(const char* format, bool printMilliseconds) override {
        if (format) {
            _timeFormat = format;
            _printMilliseconds = printMilliseconds;
        } else {
            _timeFormat.clear();
            _printMilliseconds = false;
        }
    }

    void write_message(const LogMessageHeader& header, const char* buf, size_t size) override {
        char timestampFormatted[MAX_TIMESTAMP_SIZE];
        char headerFormatted[MAX_HEADER_SIZE];
        if (!_timeFormat.empty()) {
            format_timestamp(timestampFormatted, MAX_TIMESTAMP_SIZE, _timeFormat.c_str(), header.timestamp, _printMilliseconds);
        } else {
            timestampFormatted[0] = 0;
        }
        size_t headerSize = _headerFormatter(headerFormatted, MAX_HEADER_SIZE, timestampFormatted, header);
        write_impl(header.level, headerFormatted, headerSize, buf, size);
    }

    const FileNameType& get_current_file_name() override {
        static const FileNameType emptyName;
        return emptyName;
    }

public:
    bool level_accepted(int level) override {
        return level >= _minLevel;
    }

    void write_impl(int level, const char* header, size_t headerSize, const char* msg, size_t size) {
        if (!_sink) return; 
        lock_guard<mutex> lock(_mutex);
        if (!_sink) return; // double check
        fwrite(header, 1, headerSize, _sink);
        fwrite(msg, 1, size, _sink);
        if (level >= _flushLevel) fflush(_sink);
    }
};

class ConsoleLogger : public LoggerImpl {
public:
    ConsoleLogger(int flushLevel, int consoleLevel) :
        LoggerImpl(stdout, consoleLevel, flushLevel)
    {}

    // does nothing for console
    void rotate() override {}
};

class FileLogger : public LoggerImpl {
public:
    FileLogger(int flushLevel, int minLevel, const string& fileNamePrefix, const string& dstPath) :
        LoggerImpl(0, minLevel, flushLevel),
        _fileNamePrefix(fileNamePrefix),
        _dstPath(dstPath)
    {
        open_new_file();
    }

    void rotate() override {
        try {
            open_new_file();
        } catch (const std::exception& e) {
            fprintf(stderr, "log error, %s\n", e.what());
        }
    }

    ~FileLogger() {
        fclose(_sink);
    }

    const FileNameType& get_current_file_name() override {
        return _fullPath;
    }

private:
    void open_new_file() {
        lock_guard<mutex> lock(_mutex);
        if (_sink != nullptr) {
            fclose(_sink);
            _sink = nullptr;
        }

        string fileName(_fileNamePrefix);
        fileName += format_timestamp("%y_%m_%d_%H_%M_%S", local_timestamp_msec(), false);
        fileName += ".log";

        if (!_dstPath.empty())
        {
#ifdef WIN32
            boost::filesystem::path path{ Utf8toUtf16(_dstPath.c_str()) };
#else
            boost::filesystem::path path{ _dstPath.c_str() };
#endif

            if (!boost::filesystem::exists(path))
            {
                boost::filesystem::create_directories(path);
            }

            path /= fileName;
#ifdef WIN32
            _fullPath = path.wstring();
#else
            _fullPath = path.string();
#endif
        }
        else
        {
#ifdef WIN32
            _fullPath = Utf8toUtf16(fileName.c_str());
#else
            _fullPath = fileName;
#endif
        }

#ifdef WIN32
        _sink = _wfsopen(_fullPath.c_str(), L"ab", _SH_DENYNO);
#else
        _sink = fopen(_fullPath.c_str(), "ab");
#endif

        if (!_sink) throw runtime_error(string("cannot open file ") + fileName);
    }

    std::string _fileNamePrefix;
    std::string _dstPath;

#ifdef WIN32
    std::wstring _fullPath;
#else
    std::string _fullPath;
#endif
};

class CombinedLogger : public LoggerImpl {
    FileLogger _fileSink;
    ConsoleLogger _consoleSink;

public:
    CombinedLogger(int flushLevel, int consoleLevel, int fileLevel, const std::string& fileNamePrefix, const string& dstPath) :
        LoggerImpl(0, min(fileLevel, consoleLevel), flushLevel),
        _fileSink(flushLevel, fileLevel, fileNamePrefix, dstPath),
        _consoleSink(flushLevel, consoleLevel)
    {}

    void write_message(const LogMessageHeader& header, const char* buf, size_t size) override {
        char timestampFormatted[MAX_TIMESTAMP_SIZE];
        char headerFormatted[MAX_HEADER_SIZE];
        if (!_timeFormat.empty()) {
            format_timestamp(timestampFormatted, MAX_TIMESTAMP_SIZE, _timeFormat.c_str(), header.timestamp, _printMilliseconds);
        } else {
            timestampFormatted[0] = 0;
        }
        size_t headerSize = _headerFormatter(headerFormatted, MAX_HEADER_SIZE, timestampFormatted, header);
        if (_consoleSink.level_accepted(header.level)) {
            _consoleSink.write_impl(header.level, headerFormatted, headerSize, buf, size);
        }
        if (_fileSink.level_accepted(header.level)) {
            _fileSink.write_impl(header.level, headerFormatted, headerSize, buf, size);
        }
    }

    const FileNameType& get_current_file_name() override {
        return _fileSink.get_current_file_name();
    }

    void rotate() override {
        _fileSink.rotate();
    }
};

std::shared_ptr<Logger> Logger::create(
    int flushLevel,
    int consoleLevel,
    int fileLevel,
    const std::string& fileNamePrefix,
    const std::string& dstPath
) {
    if (g_logger) {
        throw runtime_error("logger already initialized");
    }

    std::shared_ptr<Logger> logger;

    int what = 0;

    if (consoleLevel > 0) what += 1;
    if (fileLevel > 0) what += 2;

    switch (what) {
        case 3:
            logger.reset(new CombinedLogger(flushLevel, consoleLevel, fileLevel, fileNamePrefix, dstPath));
            break;
        case 2:
            logger.reset(new FileLogger(flushLevel, fileLevel, fileNamePrefix, dstPath));
            break;
        case 1:
            logger.reset(new ConsoleLogger(flushLevel, consoleLevel));
            break;
        default:
            throw runtime_error("no logger sink configured");
    }

    g_logger = logger.get();
    return logger;
}

namespace {

static constexpr size_t MAX_MSG_SIZE = 10000;

struct LogThreadContext {
    using Formatter = boost::iostreams::filtering_ostream;

    std::string msgBuffer;
    std::unique_ptr<Formatter> formatter;
    bool in_use = false;

    LogThreadContext() :
        formatter(std::make_unique<Formatter>(boost::iostreams::back_inserter(msgBuffer)))
    {}

    void reset() {
        msgBuffer = std::string();
        formatter = std::make_unique<Formatter>(boost::iostreams::back_inserter(msgBuffer));
    }
};

LogThreadContext* get_context() {
    static thread_local LogThreadContext ctx;
    return &ctx;
}

} //namespace

LogMessageHeader::LogMessageHeader(int _level, const char* _file, int _line, const char* _func) :
    timestamp(local_timestamp_msec()),
    func(_func),
    file(_file),
    line(_line),
    level(_level)
{
    if (!func) func = "";
    if (!file) {
        file = "";
    } else {
#ifdef PROJECT_SOURCE_DIR
        static const size_t offset = strlen(PROJECT_SOURCE_DIR)+1;
#else
        static const size_t offset = 0;
#endif
        assert(strlen(file) > offset);
        file += offset;
    }
}

LogMessage::LogMessage(int _level, const char* _file, int _line, const char* _func) :
    header(_level, _file, _line, _func)
{
    init_formatter();
}

LogMessage::LogMessage(const LogMessageHeader& h) :
    header(h)
{
    init_formatter();
}

void LogMessage::init_formatter() {
    LogThreadContext* ctx = get_context();
    assert(!ctx->in_use);

    ctx->in_use = true;

    if (ctx->msgBuffer.capacity() < MAX_MSG_SIZE) {
        ctx->msgBuffer.reserve(MAX_MSG_SIZE);
    }

    _formatter = ctx->formatter.get();
}

LogMessage::~LogMessage() {
    if (Logger::g_logger && _formatter) {
        *_formatter << '\n';
        _formatter->flush();
        std::string& buffer = get_context()->msgBuffer;
        Logger::g_logger->write_message(header, buffer.data(), buffer.size());
        if (buffer.size() > MAX_MSG_SIZE) {
            get_context()->reset();
        }
        else {
            buffer.clear();
        }
        get_context()->in_use = false;
    }
}

} //namespace
