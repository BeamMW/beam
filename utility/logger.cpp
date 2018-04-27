#include "logger_checkpoints.h"
#include "helpers.h"
#include <boost/interprocess/streams/bufferstream.hpp>
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
    explicit LoggerImpl(int minLevel, int flushLevel) :
        _minLevel(minLevel),
        _flushLevel(flushLevel)
    {
        assert(minLevel > 0);
    }

    virtual ~LoggerImpl() {
        if (this == g_logger) {
            g_logger = 0;
        }
    }

    virtual bool level_accepted(int level) {
        // checkpoints use level==0 which is translated according to config
        return level==0 || level >= _minLevel;
    }

    int _minLevel;
    int _flushLevel;
};

class ConsoleLogger : public LoggerImpl {
    mutex _mutex;
public:
    ConsoleLogger(const LoggerConfig& config) :
        LoggerImpl(config.consoleLevel, config.flushLevel)
    {}

    void write_message(int level, const char* buf, size_t size) override {
        lock_guard<mutex> lock(_mutex);
        cout.write(buf, size);
        if (level >= _flushLevel) cout.flush();
    }
};

class FileLogger : public LoggerImpl {
    mutex _mutex;
    ofstream _os;
public:
    FileLogger(const LoggerConfig& config) :
        LoggerImpl(config.consoleLevel, config.flushLevel)
    {
        string fileName(config.filePrefix);
#ifndef _WIN32
        fileName += to_string(getpid());
#endif
        fileName += ".log";
        _os.open(fileName);
        if (!_os) throw runtime_error(string("cannot open file ") + fileName);
    }

    void write_message(int level, const char* buf, size_t size) override {
        lock_guard<mutex> lock(_mutex);
        _os.write(buf, size);
        if (level >= _flushLevel) _os.flush();
    }
};

class CombinedLogger : public LoggerImpl {
    FileLogger _fileSink;
    ConsoleLogger _consoleSink;

public:
    CombinedLogger(const LoggerConfig& config) :
        LoggerImpl(min(config.fileLevel, config.consoleLevel), config.flushLevel),
        _fileSink(config),
        _consoleSink(config)
    {}

    void write_message(int level, const char* buf, size_t size) override {
        if (_consoleSink.will_log(level)) _consoleSink.write_message(level, buf, size);
        if (_fileSink.will_log(level)) _fileSink.write_message(level, buf, size);
    }
};

std::shared_ptr<Logger> Logger::create(const LoggerConfig& config) {
    if (g_logger) {
        throw runtime_error("logger already initialized");
    }

    std::shared_ptr<Logger> logger;

    int what = 0;

    if (config.consoleLevel > 0) what += 1;
    if (config.fileLevel > 0) what += 2;

    switch (what) {
        case 3:
            logger.reset(new CombinedLogger(config));
            break;
        case 2:
            logger.reset(new FileLogger(config));
            break;
        case 1:
            logger.reset(new ConsoleLogger(config));
            break;
        default:
            throw runtime_error("no logger sink configured");
    }

    g_logger = logger.get();
    return logger;
}

namespace {

// TODO growable streams
static constexpr size_t MAX_MSG_SIZE = 10000;

struct LogThreadContext {
    boost::interprocess::obufferstream formatter;
    char buffer[MAX_MSG_SIZE];
    bool in_use = false;
};

LogThreadContext* get_context() {
    static thread_local LogThreadContext ctx;
    return &ctx;
}

} //namespace

LogMessage::LogMessage(int level, const char* file, int line, const char* func) : _level(level) {
    LogThreadContext* ctx = get_context();



    assert(!ctx->in_use);

    ctx->in_use = true;
    ctx->formatter.clear();
    ctx->formatter.buffer(ctx->buffer, MAX_MSG_SIZE);
    _formatter = &ctx->formatter;

    static const char logTags[] = "~VDIWEC";
    assert(level < int(sizeof(logTags)));
    char b[80];
    format_timestamp(b, 80, " %Y-%m-%d.%T", local_timestamp_msec());
    *_formatter << logTags[level] << b << From(file, line, func);


}

LogMessage::~LogMessage() {
    if (_formatter) {
        *_formatter << '\n';
        Logger* logger = Logger::get();
        get_context()->in_use = false;
        if (!logger) return;
        logger->write_message(_level, get_context()->buffer, _formatter->tellp());

    }

}

} //namespace
