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

#include "utility/io/tcpserver.h"
#include "utility/io/timer.h"
#include <assert.h>

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif
#include "utility/logger.h"

using namespace beam;
using namespace beam::io;
using namespace std;

Reactor::Ptr reactor;
Timer::Ptr timer;

bool wasAccepted=false;

#ifdef __linux__
    uint32_t serverIp=0x7F222222;
    uint32_t clientIp=0x7F121314;
#else
    uint32_t serverIp=0x7F000001;
    uint32_t clientIp=0x7F000001;
#endif
uint16_t serverPort=33333;

void on_timer() {
    timer->cancel();
    reactor->tcp_connect(Address(serverIp, serverPort), 1, [](uint64_t, shared_ptr<TcpStream>&&, int){}, 1000, false, Address(clientIp, 0));
}

void tcpserver_test() {
    try {
        reactor = Reactor::create();
        TcpServer::Ptr server = TcpServer::create(
            *reactor,
            Address(serverIp, serverPort),
            [](TcpStream::Ptr&& newStream, int errorCode) {
                if (errorCode == 0) {
                    LOG_DEBUG() << "Stream accepted, socket=" << newStream->address().str() << " peer=" << newStream->peer_address().str();
                    assert(newStream);
                    wasAccepted = true;
                } else {
                    LOG_ERROR() << "Error code=" << errorCode;
                }
                reactor->stop();
            }
        );

        timer = Timer::create(*reactor);
        timer->start(
            200,
            true,//false,
            on_timer
        );

        LOG_DEBUG() << "starting reactor...";
        reactor->run();
        LOG_DEBUG() << "reactor stopped";
    }
    catch (const std::exception& e) {
        LOG_ERROR() << e.what();
    }
}

int main() {
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = Logger::create(logLevel, logLevel);
    tcpserver_test();
    return wasAccepted ? 0 : 1;
}


