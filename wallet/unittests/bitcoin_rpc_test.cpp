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

namespace
{
    const unsigned TEST_PERIOD = 1000;
}

void testSuccessResponse()
{
    io::Reactor::Ptr reactor = io::Reactor::create();
    io::Timer::Ptr timer(io::Timer::create(*reactor));
    io::Reactor::Scope scope(*reactor);
    unsigned counter = 0;

    timer->start(TEST_PERIOD, false, [&reactor]() {
        reactor->stop();
    });

    BitcoinHttpServer httpServer;

    io::Address addr(io::Address::localhost(), PORT);
    BitcoinOptions options;

    options.m_address = addr;
    options.m_userName = btcUserName;
    options.m_pass = btcPass;

    Bitcoind016 bridge(*reactor, options);

    bridge.dumpPrivKey("", [&counter](const IBitcoinBridge::Error& error, const std::string& key)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::None);
        WALLET_CHECK(!key.empty());
        ++counter;
    });

    bridge.fundRawTransaction("", 2, [&counter](const IBitcoinBridge::Error& error, const std::string& tx, int pos)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::None);
        WALLET_CHECK(!tx.empty());
        ++counter;
    });

    bridge.signRawTransaction("", [&counter](const IBitcoinBridge::Error& error, const std::string& tx, bool complete)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::None);
        WALLET_CHECK(!tx.empty());
        WALLET_CHECK(complete);
        ++counter;
    });

    bridge.sendRawTransaction("", [&counter](const IBitcoinBridge::Error& error, const std::string& txID)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::None);
        WALLET_CHECK(!txID.empty());
        ++counter;
    });

    bridge.getRawChangeAddress([&counter](const IBitcoinBridge::Error& error, const std::string& address)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::None);
        WALLET_CHECK(!address.empty());
        ++counter;
    });

    bridge.createRawTransaction("", "", 2, 0, 2, [&counter](const IBitcoinBridge::Error& error, const std::string& tx)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::None);
        WALLET_CHECK(!tx.empty());
        ++counter;
    });

    bridge.getTxOut("", 2, [&counter](const IBitcoinBridge::Error& error, const std::string& script, double value, uint16_t confirmations)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::None);
        WALLET_CHECK(!script.empty());
        WALLET_CHECK(value > 0);
        WALLET_CHECK(confirmations > 0);
        ++counter;
    });

    bridge.getBlockCount([&counter](const IBitcoinBridge::Error& error, uint64_t blocks)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::None);
        WALLET_CHECK(blocks > 0);
        ++counter;
    });

    bridge.getBalance(2, [&counter](const IBitcoinBridge::Error& error, double balance)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::None);
        WALLET_CHECK(balance > 0);
        ++counter;
    });

    reactor->run();

    WALLET_CHECK(counter == 9);
}

void testWrongCredentials()
{
    io::Reactor::Ptr reactor = io::Reactor::create();
    io::Timer::Ptr timer(io::Timer::create(*reactor));
    io::Reactor::Scope scope(*reactor);
    unsigned counter = 0;

    timer->start(TEST_PERIOD, false, [&reactor]() {
        reactor->stop();
    });

    BitcoinHttpServer httpServer("Bob", "123");

    io::Address addr(io::Address::localhost(), PORT);
    BitcoinOptions options;

    options.m_address = addr;
    options.m_userName = btcUserName;
    options.m_pass = btcPass;

    Bitcoind016 bridge(*reactor, options);

    bridge.getBlockCount([&counter](const IBitcoinBridge::Error& error, uint64_t blocks)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::InvalidCredentials);
        WALLET_CHECK(blocks == 0);
        ++counter;
    });

    reactor->run();
    WALLET_CHECK(counter == 1);
}

void testEmptyResult()
{
    io::Reactor::Ptr reactor = io::Reactor::create();
    io::Timer::Ptr timer(io::Timer::create(*reactor));
    io::Reactor::Scope scope(*reactor);
    unsigned counter = 0;

    timer->start(TEST_PERIOD, false, [&reactor]() {
        reactor->stop();
    });

    BitcoinHttpServerEmptyResult httpServer;

    io::Address addr(io::Address::localhost(), PORT);
    BitcoinOptions options;

    options.m_address = addr;
    options.m_userName = btcUserName;
    options.m_pass = btcPass;

    Bitcoind016 bridge(*reactor, options);

    bridge.fundRawTransaction("", 2, [&counter](const IBitcoinBridge::Error& error, const std::string& tx, int pos)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::EmptyResult);
        WALLET_CHECK(!error.m_message.empty());
        ++counter;
    });

    bridge.getTxOut("", 2, [&counter](const IBitcoinBridge::Error& error, const std::string& script, double value, uint16_t confirmations)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::None);
        WALLET_CHECK(error.m_message.empty());
        WALLET_CHECK(value == 0);
        WALLET_CHECK(script.empty());
        WALLET_CHECK(confirmations == 0);
        ++counter;
    });

    bridge.getBlockCount([&counter](const IBitcoinBridge::Error& error, uint64_t blocks)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::None);
        WALLET_CHECK(error.m_message.empty());
        WALLET_CHECK(blocks == 0);
        ++counter;
    });

    bridge.getBalance(2, [&counter](const IBitcoinBridge::Error& error, double balance)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::EmptyResult);
        WALLET_CHECK(!error.m_message.empty());
        ++counter;
    });

    reactor->run();

    WALLET_CHECK(counter == 4);
}

void testEmptyResponse()
{
    io::Reactor::Ptr reactor = io::Reactor::create();
    io::Timer::Ptr timer(io::Timer::create(*reactor));
    io::Reactor::Scope scope(*reactor);
    unsigned counter = 0;

    timer->start(TEST_PERIOD, false, [&reactor]() {
        reactor->stop();
    });

    BitcoinHttpServerEmptyResponse httpServer;

    io::Address addr(io::Address::localhost(), PORT);
    BitcoinOptions options;

    options.m_address = addr;
    options.m_userName = btcUserName;
    options.m_pass = btcPass;

    Bitcoind016 bridge(*reactor, options);

    bridge.fundRawTransaction("", 2, [&counter](const IBitcoinBridge::Error& error, const std::string& tx, int pos)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::InvalidResultFormat);
        WALLET_CHECK(!error.m_message.empty());
        ++counter;
    });

    reactor->run();

    WALLET_CHECK(counter == 1);
}

void testConnectionRefused()
{
    io::Reactor::Ptr reactor = io::Reactor::create();
    io::Timer::Ptr timer(io::Timer::create(*reactor));
    io::Reactor::Scope scope(*reactor);
    unsigned counter = 0;

    timer->start(5000, false, [&reactor]() {
        reactor->stop();
    });

    io::Address addr(io::Address::localhost(), PORT);
    BitcoinOptions options;

    options.m_address = addr;
    options.m_userName = btcUserName;
    options.m_pass = btcPass;

    Bitcoind016 bridge(*reactor, options);

    bridge.fundRawTransaction("", 2, [&counter](const IBitcoinBridge::Error& error, const std::string& tx, int pos)
    {
        WALLET_CHECK(error.m_type == IBitcoinBridge::IOError);
        WALLET_CHECK(!error.m_message.empty());
        ++counter;
    });

    reactor->run();
    WALLET_CHECK(counter == 1);
}

int main()
{
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = beam::Logger::create(logLevel, logLevel);

    testSuccessResponse();
    testWrongCredentials();
    testEmptyResult();
    testEmptyResponse();
    testConnectionRefused();

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}