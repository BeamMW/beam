#pragma once
#include <stdexcept>
#include <string>

namespace beam { namespace io {

struct Exception : public std::runtime_error {
#ifdef SHOW_CODE_LOCATION
    std::string function;
    std::string file;
    int line;
#endif
    int code;

    Exception(const char* _function, const char* _file, int _line, int _code) :
#ifdef SHOW_CODE_LOCATION
        std::runtime_error(std::string("io::Exception code=") + std::to_string(_code) + " " + _file + ":" + std::to_string(_line) + " " + _function),
        function(_function),
        file(_file),
        line(_line),
#else
        std::runtime_error(std::string("io::Exception code=") + std::to_string(_code)),
#endif
        code(_code)
    {}
};

#define IO_EXCEPTION(Code) throw io::Exception(__FUNCTION__, __FILE__, __LINE__, Code)
#define IO_EXCEPTION_IF(Code) if (Code != 0) throw io::Exception(__FUNCTION__, __FILE__, __LINE__, Code)

}} //namespaces
