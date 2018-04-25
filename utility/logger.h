#pragma once
#include <iostream>
#include <memory>
#include <type_traits>
#include <string.h>
#include <assert.h>

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif

#ifndef LOG_DEBUG_ENABLED
    #ifndef NDEBUG
        #define LOG_DEBUG_ENABLED 1
    #else
        #define LOG_DEBUG_ENABLED 0
    #endif
#endif

// API

#define LOG_LEVEL_CRITICAL 6
#define LOG_LEVEL_ERROR    5
#define LOG_LEVEL_WARNING  4
#define LOG_LEVEL_INFO     3
#define LOG_LEVEL_DEBUG    2
#define LOG_LEVEL_VERBOSE  1

#define LOG_SINK_DISABLED  0

#define LOG_MESSAGE(LEVEL) if (beam::Logger::will_log(LEVEL)) beam::LogMessage(LEVEL, __FILE__, __LINE__, __FUNCTION__)
#define LOG_CRITICAL() LOG_MESSAGE(LOG_LEVEL_CRITICAL)
#define LOG_ERROR() LOG_MESSAGE(LOG_LEVEL_ERROR)
#define LOG_WARNING() LOG_MESSAGE(LOG_LEVEL_WARNING)
#define LOG_INFO() LOG_MESSAGE(LOG_LEVEL_INFO)
#define LOG_DEBUG() if (LOG_DEBUG_ENABLED) LOG_MESSAGE(LOG_LEVEL_DEBUG)
#define LOG_VERBOSE() if (LOG_VERBOSE_ENABLED) LOG_MESSAGE(LOG_LEVEL_VERBOSE)

namespace beam {

// Logger options
struct LoggerConfig {
    int fileLevel=LOG_SINK_DISABLED;
    int consoleLevel=LOG_LEVEL_DEBUG;
    int flushLevel=LOG_LEVEL_WARNING;
    std::string filePrefix;

    // ~etc rotation
};

// Logger interface
class Logger {
public:
    static Logger* get() {
        assert(g_logger);
        return g_logger;
    }

    // RAII
    static std::shared_ptr<Logger> create(const LoggerConfig& config);
    virtual ~Logger() {}

    virtual void write_message(int level, const char* buf, size_t size) = 0;

    static bool will_log(int level) {
        return g_logger && g_logger->level_accepted(level);
    }

protected:
    virtual bool level_accepted(int level) = 0;

    static Logger* g_logger;
};

// __FILE__, __LINE__, __FUNCTION__ **references** in a single struct
struct From {
    From(const char* _file, int _line, const char* _func) :
        file(_file), line(_line), func(_func)
    {}

    From() : file(""), line(0), func("")
    {}

    const char* file;
    int line;
    const char* func;

    friend std::ostream& operator<<(std::ostream& o, const From& from) {
        // TODO header formatters
#ifdef PROJECT_SOURCE_DIR
        //std::cout << PROJECT_SOURCE_DIR ;
        //static const size_t offset = 0;
        static const size_t offset = strlen(PROJECT_SOURCE_DIR)+1;
#else
        static const size_t offset = 0;
#endif
        o << " (" << from.func << ", " << from.file + offset << ':' << from.line << ") ";
        return o;
    }
};

struct FlushCheckpoint {};
struct FlushAllCheckpoints {};

void flush_all_checkpoints(class LogMessage* to);
void flush_last_checkpoint(class LogMessage* to);

// Log message, supports operator<< and writes itself in destructor
class LogMessage {
public:
    static LogMessage create(int level, const char* file, int line, const char* func) {
        return LogMessage(level, file, line, func);
    }

    static LogMessage create(int level, const From& from) {
        return LogMessage(level, from.file, from.line, from.func);
    }

    LogMessage(int level, const char* file, int line, const char* func);

    template <class T> LogMessage& operator<<(T x) {
        if constexpr (std::is_same<T, const char*>::value) {
            *_formatter << x;
        }
        else if constexpr (std::is_same<T, FlushAllCheckpoints>::value) {
            flush_all_checkpoints(this);
        }
        else if constexpr (std::is_same<T, FlushCheckpoint>::value) {
            flush_last_checkpoint(this);
        }
        else if constexpr (std::is_pointer<T>::value) {
            if (x) {
                *_formatter << *x;
            } else *_formatter << "{null}";
        }
        else {
            *_formatter << x;
        }
        return *this;
    }

    ~LogMessage();

    LogMessage() {}

private:
    int _level=0;
    From _from;
    std::ostream* _formatter=0;
};


} //namespace

