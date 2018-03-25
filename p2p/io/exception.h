#pragma once
#include <stdexcept>
#include <string>

namespace io {

struct Exception : public std::runtime_error {
    std::string function;
    std::string file;
    int line;
    int code;

    Exception(const char* _function, const char* _file, int _line, int _code, const char* _message) :
        std::runtime_error(_message),
        function(_function),
        file(_file),
        line(_line),
        code(_code)
    {}
};

#define IO_EXCEPTION(Code, Message) throw io::Exception(__FUNCTION__, __FILE__, __LINE__, Code, Message)

} //namespace
