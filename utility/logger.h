#pragma once
#include <iostream>
#include <memory>
#include <type_traits>
#include <assert.h>

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif

#ifndef NDEBUG
    #define LOG_DEBUG_ENABLED 1
#else
    #define LOG_DEBUG_ENABLED 0
#endif

// API

#define LOG_LEVEL_CRITICAL 6
#define LOG_LEVEL_ERROR    5 
#define LOG_LEVEL_WARNING  4
#define LOG_LEVEL_INFO     3
#define LOG_LEVEL_DEBUG    2  
#define LOG_LEVEL_VERBOSE  1

#define LOG_SINK_DISABLED  0

#define LOG_MESSAGE(LEVEL) if (beam::Logger::will_log(LEVEL)) beam::LogMessage::create(LEVEL, __FILE__, __LINE__, __FUNCTION__)
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
    int consoleLevel=LOG_LEVEL_INFO;
    int checkpointsLevel=LOG_LEVEL_ERROR;
    
    // ~etc flushLevel, baseFileName, rotation
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
    virtual ~Logger();
    
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
            
    const char* file;
    int line;
    const char* func;
    
    friend std::ostream& operator<<(std::ostream& o, const From& from) {
        // TODO header formatters
        o << from.func << " (" << from.file << ':' << from.line << ')';
        return o;
    }
};

// Log message, supports operator<< and writes itself in destructor
class LogMessage {
public:
    static LogMessage create(int level, const char* file, int line, const char* func) {
        return LogMessage(level, file, line, func);
    }
    
    static LogMessage create(int level, const From& from) {
        return LogMessage(level, from.file, from.line, from.func);
    }

    template <class T> LogMessage& operator<<(const T& any) {
        *_formatter << any;
        return *this;
    }
    
    template <class T> LogMessage& operator<<(T* any) {
        if (any) {
            if constexpr (std::is_same<T*, const char*>::value) {
                *_formatter << any;
            } else {
                *_formatter << *any;
            }
        } else *_formatter << "null";
        
        // TODO format pointer as hex
        
        return *this;
    }

    ~LogMessage();

private:
    LogMessage(int level, const char* file, int line, const char* func);

    int _level;
    std::ostream* _formatter;
};


} //namespace

