// Copyright 2019 The Beam Team
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

#include "wallet/bitcoin/bitcoin_electrum.h"

#include "test_helpers.h"

WALLET_TEST_INIT

using namespace beam;
using json = nlohmann::json;

void testAddress()
{
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    BitcoinOptions options;
    options.m_chainType = wallet::SwapSecondSideChainType::Testnet;
    BitcoinElectrum electrum(*mainReactor, options);

    electrum.getRawChangeAddress([](const IBitcoinBridge::Error&, const std::string& addr)
    {
        LOG_INFO() << "address = " << addr;
    });
}

void testDumpPrivKey()
{
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    BitcoinOptions options;
    options.m_chainType = wallet::SwapSecondSideChainType::Testnet;
    BitcoinElectrum electrum(*mainReactor, options);

    electrum.dumpPrivKey("mkgTKdapn48BM8BaMTDSnd1miT1AZSjV7P", [](const IBitcoinBridge::Error&, const std::string& privateKey)
    {
        LOG_INFO() << "private key = " << privateKey;
    });
}

void testGetTxOut()
{
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    BitcoinOptions options;
    options.m_chainType = wallet::SwapSecondSideChainType::Testnet;
    BitcoinElectrum electrum(*mainReactor, options);

    electrum.getTxOut("d75ecb28d9289025037de08fb7ed894bda7a22a28657dd4694b947b4db22f2b6", 1, [mainReactor](const IBitcoinBridge::Error&, const std::string& hex, double value, uint16_t confirmations)
    {
        LOG_INFO() << "hex = " << hex;
        LOG_INFO() << "value = " << value;
        LOG_INFO() << "confirmations = " << confirmations;

        mainReactor->stop();
    });

    mainReactor->run();
}

void testGetBlockCount()
{
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    BitcoinOptions options;
    options.m_chainType = wallet::SwapSecondSideChainType::Testnet;
    BitcoinElectrum electrum(*mainReactor, options);

    electrum.getBlockCount([mainReactor](const IBitcoinBridge::Error& , uint64_t height)
    {
        LOG_INFO() << "height = " << height;
        
        mainReactor->stop();
    });

    mainReactor->run();
}

void testListUnspent()
{
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    BitcoinOptions options;
    options.m_chainType = wallet::SwapSecondSideChainType::Testnet;
    BitcoinElectrum electrum(*mainReactor, options);

    electrum.listUnspent([mainReactor](const IBitcoinBridge::Error&, const std::vector<BitcoinElectrum::BtcCoin>& coins)
    {
        for (auto coin : coins)
        {
            LOG_INFO() << "details = " << coin.m_details;
        }

        mainReactor->stop();
    });

    mainReactor->run();
}

void testGetBalance()
{
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    BitcoinOptions options;
    options.m_chainType = wallet::SwapSecondSideChainType::Testnet;
    BitcoinElectrum electrum(*mainReactor, options);

    electrum.getBalance(0, [mainReactor](const IBitcoinBridge::Error&, double balance)
    {
        LOG_INFO() << "balance = " << balance;

        mainReactor->stop();
    });

    mainReactor->run();
}

int main()
{
    int logLevel = LOG_LEVEL_DEBUG;
    auto logger = beam::Logger::create(logLevel, logLevel);

    /*testAddress();
    testDumpPrivKey();
    testGetTxOut();
    testGetBlockCount();*/
    //testGetBalance();
    testListUnspent();

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}