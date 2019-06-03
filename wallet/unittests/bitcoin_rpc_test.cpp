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
#include "utility/io/tcpserver.h"
#include "utility/helpers.h"
#include "nlohmann/json.hpp"

#include "wallet/bitcoin/bitcoind016.h"

#include "test_helpers.h"

WALLET_TEST_INIT

#include "bitcoin_rpc_environment.cpp"

using namespace beam;
using json = nlohmann::json;

void testSuccessResponse()
{
    io::Reactor::Ptr reactor = io::Reactor::create();
    io::Timer::Ptr timer(io::Timer::create(*reactor));
    io::Reactor::Scope scope(*reactor);

    timer->start(5000, false, [&reactor]() {
        reactor->stop();
    });

    BitcoinHttpServer httpServer;

    io::Address addr(io::Address::localhost(), PORT);
    BitcoinOptions options;

    options.m_address = addr;
    options.m_userName = "Alice";
    options.m_pass = "123";

    Bitcoind016 bridge(*reactor, options);

    bridge.dumpPrivKey("", [](const IBitcoinBridge::Error& error, const std::string& key)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::None);
        WALLET_CHECK(!key.empty());
    });

    bridge.fundRawTransaction("", 2, [](const IBitcoinBridge::Error& error, const std::string& tx, int pos)
    {
        LOG_INFO() << error.m_type;
        WALLET_CHECK(error.m_type == IBitcoinBridge::None);
        WALLET_CHECK(!tx.empty());
    });

    bridge.signRawTransaction("", [](const IBitcoinBridge::Error& error, const std::string& tx, bool complete)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::None);
        WALLET_CHECK(!tx.empty());
        WALLET_CHECK(complete);
    });

    bridge.sendRawTransaction("", [](const IBitcoinBridge::Error& error, const std::string& txID)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::None);
        WALLET_CHECK(!txID.empty());
    });

    bridge.getRawChangeAddress([](const IBitcoinBridge::Error& error, const std::string& address)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::None);
        WALLET_CHECK(!address.empty());
    });

    bridge.createRawTransaction("", "", 2, 0, 2, [](const IBitcoinBridge::Error& error, const std::string& tx)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::None);
        WALLET_CHECK(!tx.empty());
    });

    bridge.getTxOut("", 2, [](const IBitcoinBridge::Error& error, const std::string& script, double value, uint16_t confirmations)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::None);
        WALLET_CHECK(!script.empty());
        WALLET_CHECK(value > 0);
        WALLET_CHECK(confirmations > 0);
    });

    bridge.getBlockCount([](const IBitcoinBridge::Error& error, uint64_t blocks)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::None);
        WALLET_CHECK(blocks > 0);
    });

    bridge.getBalance(2, [](const IBitcoinBridge::Error& error, double balance)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::None);
        WALLET_CHECK(balance > 0);
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

    testSuccessResponse();
    return 0;
}