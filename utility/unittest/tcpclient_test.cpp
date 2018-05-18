#include "utility/io/reactor.h"
#include "utility/io/tcpstream.h"
#include "utility/io/timer.h"
#include "utility/config.h"
#include <iostream>
#include <assert.h>

#define LOG_VERBOSE_ENABLED 0
#include "utility/logger.h"

using namespace beam;
using namespace beam::io;
using namespace std;

Reactor::Ptr reactor;
TcpStream::Ptr theStream;

uint64_t tag_ok = 100;
uint64_t tag_refused = 101;
uint64_t tag_cancelled = 102;
uint64_t tag_timedout = 103;

int errorlevel = 0;
int callbackCount = 3;

void on_recv(ErrorCode what, void* data, size_t size) {
    if (data && size) {
        LOG_DEBUG() << "RECEIVED " << size << " bytes";
        LOG_VERBOSE() << "\n" << std::string((const char*)data, size);
        --callbackCount;
    } else {
        LOG_DEBUG() << "ERROR: " << error_str(what);
    }
};

void on_connected (uint64_t tag, unique_ptr<TcpStream>&& newStream, ErrorCode status) {
    if (newStream) {
        assert(status == EC_OK);
        if (tag != tag_ok) ++errorlevel;
        theStream = move(newStream);
        theStream->enable_read(on_recv);
        static const char* request = "GET / HTTP/1.0\r\n\r\n";
        theStream->write(request, strlen(request));
    } else {
        LOG_DEBUG() << "ERROR: " << error_str(status);
        if (status == EC_ECONNREFUSED && tag != tag_refused) ++errorlevel;
        if (status == EC_ETIMEDOUT && tag != tag_timedout) ++errorlevel;
        --callbackCount;
    }
};

void tcpclient_test() {
    Config config;
    config.set<Config::Int>("io.connect_timer_resolution", 1);
    reset_global_config(std::move(config));
    try {
        reactor = Reactor::create();

        Address a;
        // NOTE that this is blocked resolver, TODO add async resolver to Reactor
        a.resolve("beam-mw.com");
        a.port(80);

        if (!reactor->tcp_connect(a, tag_ok, on_connected, 10000)) ++errorlevel;
        if (!reactor->tcp_connect(Address::localhost().port(666), tag_refused, on_connected)) ++errorlevel;
        if (!reactor->tcp_connect(a.port(666), tag_timedout, on_connected, 100)) ++errorlevel;
        if (!reactor->tcp_connect(a, tag_cancelled, on_connected)) ++errorlevel;

        reactor->cancel_tcp_connect(tag_cancelled);

        Timer::Ptr timer = Timer::create(reactor);
        int x = 15;
        timer->start(200, true, [&x]{
            if (--x == 0 || callbackCount == 0) {
                reactor->stop();
            }
        });

        LOG_DEBUG() << "starting reactor...";
        reactor->run();
        LOG_DEBUG() << "reactor stopped";
    }
    catch (const Exception& e) {
        LOG_ERROR() << e.what();
    }
}

int writecancelInProgress=1;

void on_connected_writecancel(uint64_t tag, unique_ptr<TcpStream>&& newStream, ErrorCode status) {
    if (newStream) {
        assert(status == EC_OK);
        if (tag != tag_ok) ++errorlevel;
        theStream = move(newStream);
        static const char* request = "GET / HTTP/1.0\r\n\r\n";
        theStream->write(request, strlen(request));
        theStream.reset();
        writecancelInProgress=0;
    } else {
        LOG_DEBUG() << "ERROR: " << error_str(status);
    }
};

void tcpclient_writecancel_test() {
    try {
        reactor = Reactor::create();

        Address a;
        // NOTE that this is blocked resolver, TODO add async resolver to Reactor
        a.resolve("beam-mw.com");
        a.port(80);

        if (!reactor->tcp_connect(a, tag_ok, on_connected_writecancel, 10000)) ++errorlevel;

        Timer::Ptr timer = Timer::create(reactor);
        int x = 15;
        timer->start(200, true, [&x]{
            if (--x == 0 || !writecancelInProgress) {
                reactor->stop();
            }
        });

        LOG_DEBUG() << "starting reactor...";
        reactor->run();
        LOG_DEBUG() << "reactor stopped";
    }
    catch (const Exception& e) {
        LOG_ERROR() << e.what();
    }
}

int main() {
    LoggerConfig lc;
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    lc.consoleLevel = logLevel;
    lc.flushLevel = logLevel;
    auto logger = Logger::create(lc);
    tcpclient_test();
    tcpclient_writecancel_test();
    int retCode=errorlevel + callbackCount + writecancelInProgress;
    if (retCode != 0) {
        LOG_ERROR() << TRACE(errorlevel) << TRACE(callbackCount) << TRACE(writecancelInProgress);
    }
    return retCode;
}



