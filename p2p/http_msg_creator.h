#pragma once
#include "fragment_writer.h"

namespace beam {

struct HeaderPair {
    const char* head;
    union {
        const char* content_str;
        unsigned long content_num;
    };
    bool is_number;

    HeaderPair(const char* h="", const char* c="") :
        head(h), content_str(c), is_number(false)
    {}

    HeaderPair(const char* h, unsigned long n) :
        head(h), content_num(n), is_number(true)
    {}
};

class HttpMsgCreator {
public:
    explicit HttpMsgCreator(size_t fragmentSize) :
        _fragmentWriter(
            fragmentSize,
            16,
            [this](io::SharedBuffer&& fragment) {
                if (_currentMsg) _currentMsg->push_back(std::move(fragment));
            }
        ),
        _currentMsg(0)
    {}

    bool create_request(
        io::SerializedMsg& out,
        const char* method,
        const char* path,
        const HeaderPair* headers, size_t num_headers,
        int http_minor_version = 1,
        const char* content_type = "",
        const io::SharedBuffer& body = io::SharedBuffer()
    );

    bool create_response(
        io::SerializedMsg& out,
        int code,
        const char* message,
        const HeaderPair* headers, size_t num_headers,
        int http_minor_version = 1,
        const char* content_type = 0,
        const io::SharedBuffer& body = io::SharedBuffer()
    );

private:
    FragmentWriter _fragmentWriter;
    io::SerializedMsg* _currentMsg;
};

} //namespace
