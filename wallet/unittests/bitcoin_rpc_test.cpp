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

#include "utility/logger.h"
#include "utility/io/timer.h"
#include "wallet/bitcoin_rpc.h"

using namespace beam;

void testWithBitcoinD()
{
    
    io::Reactor::Ptr reactor = io::Reactor::create();
    io::Timer::Ptr timer(io::Timer::create(*reactor));

    timer->start(5000, false, [&reactor]() {
        reactor->stop();
    });

    io::Address addr(io::Address::localhost(), 18443);
    BitcoinRPC rpc(*reactor, "test", "123", addr);

    rpc.getBlockchainInfo([&reactor, &rpc](const std::string& result) {
        LOG_INFO() << "getblockchaininfo result: " << result;
        
        return false;
    });

    rpc.getNetworkInfo([&reactor, &rpc](const std::string& result) {
        LOG_INFO() << "getnetworkinfo result: " << result;

        return false;
    });

    rpc.getWalletInfo([&reactor, &rpc](const std::string& result) {
        LOG_INFO() << "getwalletinfo result: " << result;

        return false;
    });

    rpc.estimateFee(3, [&reactor, &rpc](const std::string& result) {
        LOG_INFO() << "estimateFee result: " << result;

        return false;
    });

    rpc.getRawChangeAddress([&reactor, &rpc](const std::string& result) {
        LOG_INFO() << "getRawChangeAddress result: " << result;

        return false;
    });

    rpc.getBalance([&reactor, &rpc](const std::string& result) {
        LOG_INFO() << "getbalance result: " << result;

        return false;
    });

    rpc.dumpPrivKey("mhYozJvft4LdsTkGbsXEAifSmcNMNsqFpF", [&reactor, &rpc](const std::string& result) {
        LOG_INFO() << "dumpprivkey result: " << result;
        //reactor->stop();
        return false;
    });

    reactor->run();
}

int main()
{
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = beam::Logger::create(logLevel, logLevel);

    testWithBitcoinD();
    return 0;
}