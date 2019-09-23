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

#include "utility/io/reactor.h"
#include "utility/io/sslstream.h"
#include "utility/io/timer.h"
#include "utility/config.h"
#include <iostream>
#include <assert.h>

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif
#include "utility/logger.h"

using namespace beam;
using namespace beam::io;
using namespace std;

std::vector<TcpStream::Ptr> streams;

uint64_t tag_ok = 100;
uint64_t tag_refused = 101;
uint64_t tag_cancelled = 102;
uint64_t tag_timedout = 103;

int errorlevel = 0;
int callbackCount = 0;
int writecancelInProgress=0;
bool g_FirstRcv = true;

int calc_errors() {
    int retCode=errorlevel + callbackCount + writecancelInProgress;
    if (retCode != 0) {
        LOG_ERROR() << TRACE(errorlevel) << TRACE(callbackCount) << TRACE(writecancelInProgress);
        errorlevel=0;
        callbackCount=0;
        writecancelInProgress=0;
    }
    return retCode;
}

#define DOMAIN_NAME "beam.mw"

bool on_recv(ErrorCode what, void* data, size_t size) {
    if (data && size) {
        LOG_DEBUG() << "RECEIVED " << size << " bytes";
        LOG_DEBUG() << "\n" << std::string((const char*)data, size);
		if (g_FirstRcv)
		{
			g_FirstRcv = false;
			--callbackCount;
		}
    } else {
        LOG_DEBUG() << __FUNCTION__ << " ERROR: " << error_str(what);
    }
    return true;
};

void on_connected (uint64_t tag, unique_ptr<TcpStream>&& newStream, ErrorCode status) {
    if (newStream) {
        assert(status == EC_OK);
        if (tag != tag_ok) ++errorlevel;
        newStream->enable_read(on_recv);
        static const char* request = "GET / HTTP/1.0\r\nHost: " DOMAIN_NAME "\r\n\r\n";
        Result res = newStream->write(request, strlen(request));
        if (!res) {
            LOG_ERROR() << error_str(res.error());
        }
        streams.emplace_back(move(newStream));

    } else {
        LOG_DEBUG() << __FUNCTION__ << " ERROR: " << error_str(status);
        if (status == EC_ECONNREFUSED && tag != tag_refused) ++errorlevel;
        if (status == EC_ETIMEDOUT && tag != tag_timedout) ++errorlevel;
        --callbackCount;
    }
};

int tcpclient_test(bool ssl) {
    callbackCount = 3;
    g_FirstRcv = true;
    try {
        Reactor::Ptr reactor = Reactor::create();

        Address a;
        // NOTE that this is blocked resolver, TODO add async resolver to Reactor
        a.resolve(DOMAIN_NAME);
        a.port(ssl ? 443 : 80);

        if (!reactor->tcp_connect(a, tag_ok, on_connected, 10000, ssl)) ++errorlevel;
        if (!reactor->tcp_connect(Address::localhost().port(666), tag_refused, on_connected, -1, ssl)) ++errorlevel;
        if (!reactor->tcp_connect(a.port(666), tag_timedout, on_connected, 100, ssl)) ++errorlevel;
        if (!reactor->tcp_connect(a, tag_cancelled, on_connected, -1, ssl)) ++errorlevel;

        reactor->cancel_tcp_connect(tag_cancelled);

        Timer::Ptr timer = Timer::create(*reactor);
        int x = 20;
        timer->start(200, true, [&x, &reactor]{
            if (--x == 0 || callbackCount == 0) {
                reactor->stop();
            }
        });

        LOG_DEBUG() << "starting reactor...";
        reactor->run();
        LOG_DEBUG() << "reactor stopped";

        streams.clear();
        LOG_DEBUG() << TRACE(reactor.use_count());
    }
    catch (const Exception& e) {
        LOG_ERROR() << e.what();
    }

    return calc_errors();
}

void on_connected_writecancel(uint64_t tag, unique_ptr<TcpStream>&& newStream, ErrorCode status) {
    LOG_DEBUG() << "on_connected_writecancel: " << error_str(status);
    if (newStream) {
        assert(status == EC_OK);
        if (tag != tag_ok) ++errorlevel;
        static const char* request = "GET / HTTP/1.0\r\n\r\n";
        newStream->write(request, strlen(request));
        writecancelInProgress=0;
    } else {
        LOG_DEBUG() << __FUNCTION__ << " ERROR: " << error_str(status);
    }
};

int tcpclient_writecancel_test() {
    try {
        writecancelInProgress=1;
        Reactor::Ptr reactor = Reactor::create();

        Address a;
        // NOTE that this is blocked resolver, TODO add async resolver to Reactor
        a.resolve(DOMAIN_NAME);
        a.port(80);

        if (!reactor->tcp_connect(a, tag_ok, on_connected_writecancel, 10000)) ++errorlevel;

        Timer::Ptr timer = Timer::create(*reactor);
        int x = 15;
        timer->start(200, true, [&x, &reactor]{
            if (--x == 0 || !writecancelInProgress) {
                reactor->stop();
            }
        });

        LOG_DEBUG() << "starting reactor...";
        reactor->run();
        LOG_DEBUG() << "reactor stopped";

        LOG_DEBUG() << TRACE(reactor.use_count());
        reactor.reset();
        streams.clear();
    }
    catch (const Exception& e) {
        LOG_ERROR() << e.what();
    }

    return calc_errors();
}

void on_connected_dummy(uint64_t tag, unique_ptr<TcpStream>&& newStream, ErrorCode status) {
    LOG_DEBUG() << "on_connected_dummy: " << error_str(status);
};

int tcpclient_unclosed_test() {
    try {
        Reactor::Ptr reactor = Reactor::create();

        //Address a = Address::localhost().port(80);
        Address a;
        // NOTE that this is blocked resolver, TODO add async resolver to Reactor
        a.resolve(DOMAIN_NAME);
        a.port(80);


        for (uint64_t i=0; i<9; ++i) {
            auto result = reactor->tcp_connect(a, i, on_connected_dummy, 10000);
            if (!result) {
                LOG_ERROR() << error_descr(result.error());
                ++errorlevel;
            }
        }


        Timer::Ptr timer = Timer::create(*reactor);
        timer->start(6, false, [&reactor]{
            reactor->stop();
        });


        LOG_DEBUG() << "starting reactor...";
        reactor->run();
        LOG_DEBUG() << "reactor stopped";

        streams.clear();
    }
    catch (const Exception& e) {
        LOG_ERROR() << e.what();
    }

    return calc_errors();
}

int main() {
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = Logger::create(logLevel, logLevel);

    Config config;
    config.set<Config::Int>("io.connect_timer_resolution", 1);
    reset_global_config(std::move(config));

    int retCode = 0;
    retCode += tcpclient_test(false);
    retCode += tcpclient_test(true);
    retCode += tcpclient_writecancel_test();
    retCode += tcpclient_unclosed_test();
    return retCode;
}



