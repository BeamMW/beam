// Copyright 2020 The Beam Team
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

#include "test_helpers.h"

#include "wallet/transactions/swaps/bridges/ethereum/ethereum.h"

WALLET_TEST_INIT

using namespace beam;
using namespace std;

namespace beam::ethereum
{
    class Provider : public ISettingsProvider
    {
    public:
        Provider(const Settings& settings)
            : m_settings(settings)
        {
        }

        Settings GetSettings() const override
        {
            return m_settings;
        }

        void SetSettings(const Settings& settings) override
        {
            m_settings = settings;
        }

        bool CanModify() const override
        {
            return true;
        }

        void AddRef() override
        {
        }

        void ReleaseRef() override
        {

        }

    private:
        Settings m_settings;
    };
}

void testAddress()
{
    std::cout << "\nTesting generation of ethereum address...\n";

    ethereum::Settings settings;
    settings.m_secretWords = { "grass", "happy", "napkin", "skill", "hazard", "isolate", "slot", "barely", "stamp", "dismiss", "there", "found" };
    settings.m_accountIndex = 0;

    auto provider = std::make_shared<ethereum::Provider>(settings);
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    ethereum::EthereumBridge bridge(*mainReactor, *provider);

    std::cout << bridge.generateEthAddress() << std::endl;
}

void testBalance()
{
    std::cout << "\nTesting balance...\n";

    ethereum::Settings settings;
    settings.m_secretWords = { "grass", "happy", "napkin", "skill", "hazard", "isolate", "slot", "barely", "stamp", "dismiss", "there", "found" };
    settings.m_accountIndex = 0;
    settings.m_address = "127.0.0.1:7545";
    settings.m_shouldConnect = true;

    auto provider = std::make_shared<ethereum::Provider>(settings);
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    ethereum::EthereumBridge bridge(*mainReactor, *provider);

    //std::cout << bridge.generateEthAddress() << std::endl;

    bridge.getBalance([mainReactor](ECC::uintBig balance)
    {
        std::cout << balance << std::endl;
        mainReactor->stop();
    });

    mainReactor->run();
}

void testBlockNumber()
{
    std::cout << "\nTesting block number...\n";

    ethereum::Settings settings;
    settings.m_secretWords = { "grass", "happy", "napkin", "skill", "hazard", "isolate", "slot", "barely", "stamp", "dismiss", "there", "found" };
    settings.m_accountIndex = 0;
    settings.m_address = "127.0.0.1:7545";
    settings.m_shouldConnect = true;

    auto provider = std::make_shared<ethereum::Provider>(settings);
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    ethereum::EthereumBridge bridge(*mainReactor, *provider);

    //std::cout << bridge.generateEthAddress() << std::endl;

    bridge.getBlockNumber([mainReactor](Amount blockNumber)
    {
        std::cout << blockNumber << std::endl;
        mainReactor->stop();
    });

    mainReactor->run();
}

int main()
{
    int logLevel = LOG_LEVEL_DEBUG;
    auto logger = beam::Logger::create(logLevel, logLevel);

    testAddress();
    testBalance();
    testBlockNumber();
    
    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}