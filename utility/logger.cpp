#include "logger_checkpoints.h"
#include "helpers.h"
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <mutex>
#include <algorithm>

namespace beam {

using namespace std;

Logger* Logger::g_logger = 0;

class LoggerImpl : public Logger {
    mutex _mutex;
protected:
    static const size_t MAX_HEADER_SIZE = 256;
    static const size_t MAX_TIMESTAMP_SIZE = 80;

    ostream* _sink;
    int _minLevel;
    int _flushLevel;
    LogMessageHeaderFormatter _headerFormatter = def_header_formatter;
    std::string _timeFormat;
    bool _printMilliseconds;

    LoggerImpl(ostream* sink, int minLevel, int flushLevel) :
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

public:
    bool level_accepted(int level) override {
        return level >= _minLevel;
    }

    void write_impl(int level, const char* header, size_t headerSize, const char* msg, size_t size) {
        if (!_sink) return;
        lock_guard<mutex> lock(_mutex);
        _sink->write(header, headerSize);
        _sink->write(msg, size);
        if (level >= _flushLevel) _sink->flush();
    }
};

class ConsoleLogger : public LoggerImpl {
public:
    ConsoleLogger(int flushLevel, int consoleLevel) :
        LoggerImpl(&cout, consoleLevel, flushLevel)
    {}
};

class FileLogger : public LoggerImpl {
    ofstream _os;
public:
    FileLogger(int flushLevel, int minLevel, const string& fileNamePrefix) :
        LoggerImpl(0, minLevel, flushLevel)
    {
        string fileName(fileNamePrefix);
#ifndef _WIN32
        fileName += to_string(getpid());
#endif
        fileName += ".log";
        _os.open(fileName);
        if (!_os) throw runtime_error(string("cannot open file ") + fileName);
        _sink = &_os;
    }
};

class CombinedLogger : public LoggerImpl {
    FileLogger _fileSink;
    ConsoleLogger _consoleSink;

public:
    CombinedLogger(int flushLevel, int consoleLevel, int fileLevel, const std::string& fileNamePrefix) :
        LoggerImpl(0, min(fileLevel, consoleLevel), flushLevel),
        _fileSink(flushLevel, fileLevel, fileNamePrefix),
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
};

std::shared_ptr<Logger> Logger::create(
    int flushLevel,
    int consoleLevel,
    int fileLevel,
    const std::string& fileNamePrefix
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
            logger.reset(new CombinedLogger(flushLevel, consoleLevel, fileLevel, fileNamePrefix));
            break;
        case 2:
            logger.reset(new FileLogger(flushLevel, fileLevel, fileNamePrefix));
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
