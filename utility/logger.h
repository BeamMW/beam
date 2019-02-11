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
#include <iostream>
#include <memory>
#include <type_traits>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif

#ifndef LOG_DEBUG_ENABLED
    #ifdef DEBUG_MESSAGES_IN_RELEASE_MODE
        #define LOG_DEBUG_ENABLED 1
    #else
        #ifndef NDEBUG
            #define LOG_DEBUG_ENABLED 1
        #else
            #define LOG_DEBUG_ENABLED 0
        #endif
    #endif
#endif

#ifndef SHOW_CODE_LOCATION
    #define SHOW_CODE_LOCATION 0
#endif

// API

#define LOG_LEVEL_CRITICAL 6
#define LOG_LEVEL_ERROR    5
#define LOG_LEVEL_WARNING  4
#define LOG_LEVEL_INFO     3
#define LOG_LEVEL_DEBUG    2
#define LOG_LEVEL_VERBOSE  1

#define LOG_SINK_DISABLED  0

// This stub will be optimized out;
struct LogMessageStub {
    LogMessageStub() {}
    template <typename T> LogMessageStub& operator<<(const T&) { return *this; }
};

#if SHOW_CODE_LOCATION
    #define LOG_MESSAGE(LEVEL) if (beam::Logger::will_log(LEVEL)) beam::LogMessage(LEVEL, __FILE__, __LINE__, __FUNCTION__)
#else
    #define LOG_MESSAGE(LEVEL) if (beam::Logger::will_log(LEVEL)) beam::LogMessage(LEVEL)
#endif

#define LOG_CRITICAL() LOG_MESSAGE(LOG_LEVEL_CRITICAL)
#define LOG_ERROR() LOG_MESSAGE(LOG_LEVEL_ERROR)
#define LOG_WARNING() LOG_MESSAGE(LOG_LEVEL_WARNING)
#define LOG_INFO() LOG_MESSAGE(LOG_LEVEL_INFO)
#define LOG_UNHANDLED_EXCEPTION() LOG_ERROR() << "["<< __FILE__ << "] [" << __LINE__ << "] [" << __FUNCTION__ << "] unhandled exception. "

#if LOG_DEBUG_ENABLED
    #define LOG_DEBUG() LOG_MESSAGE(LOG_LEVEL_DEBUG)
#else
    #define LOG_DEBUG() LogMessageStub()
#endif

#if LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE() LOG_MESSAGE(LOG_LEVEL_VERBOSE)
#else
    #define LOG_VERBOSE() LogMessageStub()
#endif

#define TRACE(var) " " #var "=" << var

#ifdef WIN32
#define __PRETTY_FUNCTION__ __FUNCSIG__
#endif // WIN32

namespace beam {

// Logger options
struct LoggerConfig {
    int fileLevel=LOG_SINK_DISABLED;
    int consoleLevel=LOG_LEVEL_DEBUG;
    int flushLevel=LOG_LEVEL_WARNING;
    std::string filePrefix;

    // ~etc rotation
};

struct LogMessageHeader {
    uint64_t timestamp;
    const char* func;
    const char* file;
    int line;
    int level;

    LogMessageHeader(int _level, const char* _file, int _line, const char* _func);
};

/// Returns char corresponding to log level
inline char loglevel_tag(int level) {
    static const char logTags[] = "~VDIWEC";
    if (level < 0 || level >= int(sizeof(logTags))) level = 0;
    return logTags[level];
}

/// Custom header formatter interface, must return bytes consumed in buf, not exceeding maxSize
typedef size_t (*LogMessageHeaderFormatter)(char* buf, size_t maxSize, const char* timestampFormatted, const LogMessageHeader& header);

/// Default header formatter
inline size_t def_header_formatter(char* buf, size_t maxSize, const char* timestampFormatted, const LogMessageHeader& header) {
    if (header.line)
        return snprintf(buf, maxSize, "%c %s (%s, %s:%d) ", loglevel_tag(header.level), timestampFormatted, header.func, header.file, header.line);
    return snprintf(buf, maxSize, "%c %s ", loglevel_tag(header.level), timestampFormatted);
}

/// Logger interface
class Logger {
public:
    /// RAII
    static std::shared_ptr<Logger> create(
        // flushes sinks if level >= flushLevel
        int flushLevel=LOG_LEVEL_WARNING,

        // default console minimal level, use LOG_SINK_DISABLED to disable console log
        int consoleLevel=LOG_LEVEL_DEBUG,

        // default file logger minimal level, use LOG_SINK_DISABLED to disable file log
        int fileLevel=LOG_SINK_DISABLED,

        // filename prefix, needed if file log enabled
        const std::string& fileNamePrefix = std::string(),

        // path to log file
        const std::string& dstPath = std::string()
    );

    virtual ~Logger() {}

    /// Sets custom msg header formatter, default is def_header_formatter
    virtual void set_header_formatter(LogMessageHeaderFormatter formatter) = 0;

    /// Sets custom timestamp formatter as for strftime(), default is "%Y-%m-%d.%T" and milliseconds are printed
    virtual void set_time_format(const char* format, bool printMilliseconds) = 0;

#ifdef WIN32
    using FileNameType = std::wstring;
#else
    using FileNameType = std::string;
#endif

    /// Returns curtrent log file name
    virtual const FileNameType& get_current_file_name() = 0;

    /// Rotates file name, called externally
    virtual void rotate() = 0;

    static bool will_log(int level) {
        return g_logger && g_logger->level_accepted(level);
    }

    static Logger* get() {
        assert(g_logger);
        return g_logger;
    }

protected:
    friend class LogMessage;

    virtual bool level_accepted(int level) = 0;

    /// Called from LogMessage dtor on message completed
    virtual void write_message(const LogMessageHeader& header, const char* buf, size_t size) = 0;

    static Logger* g_logger;
};

struct FlushCheckpoint {};
struct FlushAllCheckpoints {};

void flush_all_checkpoints(class LogMessage* to);
void flush_last_checkpoint(class LogMessage* to);

// Log message, supports operator<< and writes itself in destructor
class LogMessage {
public:
    LogMessageHeader header;

    LogMessage(int _level, const char* _file=0, int _line=0, const char* _func=0);

    LogMessage(const LogMessageHeader& h);

    template <class T> LogMessage& operator<<(const T& x) {
        if constexpr (std::is_same<T, FlushAllCheckpoints>::value) {
            flush_all_checkpoints(this);
        }
        else if constexpr (std::is_same<T, FlushCheckpoint>::value) {
            flush_last_checkpoint(this);
        }
        else {
            *_formatter << x;
        }
        return *this;
    }

    ~LogMessage();
private:
    void init_formatter();

    std::ostream* _formatter=0;
};


} //namespace

