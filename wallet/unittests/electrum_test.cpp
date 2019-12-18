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

#include "wallet/bitcoin/bitcoin.h"

#include "test_helpers.h"

#include "bitcoin/bitcoin.hpp"
#include "utility/io/sslserver.h"

WALLET_TEST_INIT

using namespace beam;
using namespace std;
using namespace ECC;

#include "swap_test_environment.cpp"

namespace beam::bitcoin
{
    class Provider : public ISettingsProvider
    {
    public:
        Provider(const ElectrumSettings& settings)
        {
            m_settings.SetElectrumConnectionOptions(settings);
        }

        Settings GetSettings() const override
        {
            return m_settings;
        }

        void SetSettings(const bitcoin::Settings& settings) override
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

//using namespace beam;
using json = nlohmann::json;



void testAddress()
{
    std::cout << "\nTesting generation of electrum address...\n";

    bitcoin::ElectrumSettings settings;
    settings.m_secretWords = { "child", "happy", "moment", "weird", "ten", "token", "stuff", "surface", "success", "desk", "embark", "observe" };

    auto provider = std::make_shared<bitcoin::Provider>(settings);
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    bitcoin::Electrum electrum(*mainReactor, *provider);

#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
    std::set<std::string> addresses
    {
        "16AW2aVqy2gva1hxdtF4xhoSrTQTbGSNvM",
        "1HC9pxNaEHSFWgsiHUDUM3QRETNqBmSQQ9",
        "13V8K8wnLnLfiYaD3JHxTHg2SVTzdJyhgs",
        "1gJnvfcWt9W7sboLWW15yDUzd3idFUUvp",
        "1NGH4WK29VnNdPBJVyS6hx1t9z8Dtx1mUW",
        "1BuUxbUAn84q4bM3uf2k2zDZZ7bzwRP3Hh",
        "1KHdjosUP69aExUMqqq6CLH77bdR5oRoHS",
        "1DFnVotGE4pMv1LiPfw6XQptEqWeYqgzPy",
        "19LLcmPxnFiH7SrsgVXG5M7qWASEQ6cKNh",
        "1MUJs4zcrDpo1UuTMLGTViHDLbhcd66oNK",
        "1Q9RKPvseVrYebiehuXBfsZsrsdMRu6b9y",
        "1HeTXHuqiugtF4gPwLEKQFeT2VQVqa7FsW",
        "12H7k6V9wH4unyFqfUZ6s7pyaNXQQAH7fD",
        "18wjL1B6KNkBaHj8F8w2nnyKTuHrWPoNpC",
        "1KJH4e8Dwvq9CqKB2zFoUsH7tnkB8D7Wos",
        "1N38b6D4mC9R4R9SZztyvbV97uKPfiPpvp",
        "13oWcAxFsHW1n81gj7BVAM1x7qiqhvnjhp",
        "1H4u6j8yZ49FS1Ft2nvXRpRKhWzaJTcCHD",
        "1Lw1ec2Q2KsT2VcV8sXdsr3bjwkaokPE6U",
        "1Ei6D8SRSZ7w4MgDpZsGSvpqcejjqw2ewU",
        "1LjTVPhMx51QUPjUY9aVL2WgNhRzZERHZw",
};
#else
    std::set<std::string> addresses
    {
        "mkgTKdapn48BM8BaMTDSnd1miT1AZSjV7P",
        "mwi781TZ3JsWHoML13BrAxck6SyY2vxW55",
        "mi15cC2m9omvVf3pksGLHCtMJV4hWNi4B8",
        "mgCG5ykbKuaktz5R45UNutRorceRdr4hqi",
        "n2nEMZPzxXDdQVevDYQUXsED1yivmK7bqw",
        "mrRSFeZ9b9W5qhpfdE17ruRtR7ChtfzfvH",
        "myob2rxTC7aq24wyZQoU2FVRybE84CMABE",
        "msmjnryF36Fch7pL7EuUML3D6q7MUKRu68",
        "morHupUwbH9XtZLVQ4VduGLANA2wK1sX2M",
        "n1zGA85bfFG3nbP54uEqKdVYCbJKXwufM2",
        "n4fNcT1rTXHoRiCGRUVZVnnCisE4Ncgeqf",
        "mxAQpLzpXw892BA1euChEArmtV1ChKr8CC",
        "mgo539a8kJWAa5jTP3XUh33JSN87DyMJEV",
        "moTgd4G58QBSMQCjxhuQciBeKttZQ9WyLh",
        "mypEMhDCkxGPywnnkZEBJnVSknLt2DymJT",
        "n2Z5t9J3aDafqXd4HZsMkWhTytv6gqdYpF",
        "miKTuE3EgJwGZEVJSg9rzGEGyqKYafoxHo",
        "mwarPnDxN5aWD7jVkMtuFjdeZWbHDDoZi4",
        "n1Sxwf7NqMJhoc66rSW1hmFvbwMHkzCPhe",
        "muE3WBXQFaZBqU9qY8qeGr3AUeLSoYwJyx",
        "n1FQnSnLm6SfFWD6FiYs9wj1Eh2hYNYNRJ"
    };
#endif

    for (int i = 0; i < 30; i++)
    {
        electrum.getRawChangeAddress([mainReactor, addresses](const bitcoin::IBridge::Error&, const std::string& addr)
        {
            LOG_INFO() << "generated address = " << addr;

            WALLET_CHECK(addresses.find(addr) != addresses.end());
        });
    }
}

void testConnection()
{
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    io::Timer::Ptr timer(io::Timer::create(*mainReactor));
    std::string address("127.0.0.1:10400");
    TestElectrumWallet btcWallet(*mainReactor, address);
    io::Address addr;
    std::unique_ptr<io::TcpStream> sslStream;

    addr.resolve(address.c_str());

    mainReactor->tcp_connect(addr, 1, [&](uint64_t tag, std::unique_ptr<io::TcpStream>&& newStream, io::ErrorCode status)
    {
        LOG_DEBUG() << "status = " << static_cast<int>(status);
        sslStream = std::move(newStream);

        sslStream->enable_read([&](io::ErrorCode what, void* data, size_t size)
        {
            if (data && size)
            {
                LOG_DEBUG() << "result: " << std::string((const char*)data, size);
            }
            else
            {
                LOG_DEBUG() << "error what = " << static_cast<int>(what);
            }
            return true;
        });

        std::string request = "tt";

        io::Result res = sslStream->write(request.data(), request.size());
        if (!res) {
            LOG_ERROR() << error_str(res.error());
        }
    }, 2000, true);

    timer->start(5000, false, [&]() {
        mainReactor->stop();
    });

    mainReactor->run();
}

void testReconnect1()
{
    std::cout << "\nTesting connection to 3 offline and 1 online electrum servers...\n";

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    bitcoin::ElectrumSettings settings;
    settings.m_automaticChooseAddress = true;
    settings.m_secretWords = { "child", "happy", "moment", "weird", "ten", "token", "stuff", "surface", "success", "desk", "embark", "observe" };
    settings.m_nodeAddresses = {
        "127.0.0.1:11001",
        "127.0.0.1:11002",
        "127.0.0.1:11003",
        "127.0.0.1:11004"
    };

    TestElectrumWallet btcWallet(*mainReactor, settings.m_nodeAddresses.back());
    auto provider = std::make_shared<bitcoin::Provider>(settings);
    auto electrum = std::make_shared<bitcoin::Electrum>(*mainReactor, *provider);
    std::size_t ind = 0;
    std::function<void()> nextStep;
    auto callback = [&](const bitcoin::IBridge::Error& error, uint64_t)
    {
        ind++;
        if (ind < settings.m_nodeAddresses.size())
        {
            WALLET_CHECK(error.m_type == bitcoin::IBridge::IOError);
            WALLET_CHECK(provider->GetSettings().GetElectrumConnectionOptions().m_address == settings.m_nodeAddresses[ind]);
        }
        else
        {
            WALLET_CHECK(error.m_type == bitcoin::IBridge::None);
            WALLET_CHECK(provider->GetSettings().GetElectrumConnectionOptions().m_address == settings.m_nodeAddresses.back());
        }

        nextStep();
    };

    nextStep = [&]()
    {
        if (ind < settings.m_nodeAddresses.size())
            electrum->getBalance(0, callback);
        else
            mainReactor->stop();
    };

    electrum->getBalance(0, callback);
    mainReactor->run();
}

void testReconnect2()
{
    std::cout << "\nTesting connection to 4 offline electrum servers...\n";

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    bitcoin::ElectrumSettings settings;
    settings.m_automaticChooseAddress = true;
    settings.m_secretWords = { "child", "happy", "moment", "weird", "ten", "token", "stuff", "surface", "success", "desk", "embark", "observe" };
    settings.m_nodeAddresses = {
        "127.0.0.1:11001",
        "127.0.0.1:11002",
        "127.0.0.1:11003",
        "127.0.0.1:11004"
    };

    auto provider = std::make_shared<bitcoin::Provider>(settings);
    auto electrum = std::make_shared<bitcoin::Electrum>(*mainReactor, *provider);
    std::size_t ind = 0;
    std::function<void()> nextStep;
    auto callback = [&](const bitcoin::IBridge::Error& error, uint64_t)
    {
        ind++;
        WALLET_CHECK(error.m_type == bitcoin::IBridge::IOError);
        if (ind < settings.m_nodeAddresses.size())
        {
            WALLET_CHECK(provider->GetSettings().GetElectrumConnectionOptions().m_address == settings.m_nodeAddresses[ind]);
        }
        else
        {
            WALLET_CHECK(provider->GetSettings().GetElectrumConnectionOptions().m_address == settings.m_nodeAddresses.front());
        }

        nextStep();
    };

    nextStep = [&]()
    {
        if (ind < settings.m_nodeAddresses.size())
            electrum->getBalance(0, callback);
        else
            mainReactor->stop();
    };

    electrum->getBalance(0, callback);
    mainReactor->run();
}

void testConnectToOfflineNode()
{
    std::cout << "\nTesting connection to offline electrum server (automatic node selection mode is turned OFF)...\n";

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    bitcoin::ElectrumSettings settings;
    settings.m_automaticChooseAddress = false;
    settings.m_secretWords = { "child", "happy", "moment", "weird", "ten", "token", "stuff", "surface", "success", "desk", "embark", "observe" };
    settings.m_address = "127.0.0.1:12000";
    settings.m_nodeAddresses = {
        "127.0.0.1:11001",
        "127.0.0.1:11002",
        "127.0.0.1:11003",
        "127.0.0.1:11004"
    };

    auto provider = std::make_shared<bitcoin::Provider>(settings);
    auto electrum = std::make_shared<bitcoin::Electrum>(*mainReactor, *provider);
    std::size_t ind = 0;
    std::function<void()> nextStep;
    auto callback = [&](const bitcoin::IBridge::Error& error, uint64_t)
    {
        ind++;
        WALLET_CHECK(error.m_type == bitcoin::IBridge::IOError);
        WALLET_CHECK(provider->GetSettings().GetElectrumConnectionOptions().m_address == settings.m_address);

        nextStep();
    };

    nextStep = [&]()
    {
        if (ind < settings.m_nodeAddresses.size())
            electrum->getBalance(0, callback);
        else
            mainReactor->stop();
    };

    electrum->getBalance(0, callback);
    mainReactor->run();
}

void testConnectToInvalidAddress()
{
    std::cout << "\nTesting connection to invalid address (automatic node selection mode is turned OFF)...\n";

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    bitcoin::ElectrumSettings settings;
    settings.m_automaticChooseAddress = false;
    settings.m_secretWords = { "child", "happy", "moment", "weird", "ten", "token", "stuff", "surface", "success", "desk", "embark", "observe" };
    settings.m_address = "tutututu";
    settings.m_nodeAddresses = {
        "127.0.0.1:11001",
        "127.0.0.1:11002",
        "127.0.0.1:11003",
        "127.0.0.1:11004"
    };

    auto provider = std::make_shared<bitcoin::Provider>(settings);
    auto electrum = std::make_shared<bitcoin::Electrum>(*mainReactor, *provider);
    std::size_t ind = 0;
    std::function<void()> nextStep;
    auto callback = [&](const bitcoin::IBridge::Error& error, uint64_t)
    {
        ind++;
        WALLET_CHECK(error.m_type == bitcoin::IBridge::IOError);
        WALLET_CHECK(provider->GetSettings().GetElectrumConnectionOptions().m_address == settings.m_address);

        nextStep();
    };

    nextStep = [&]()
    {
        if (ind < settings.m_nodeAddresses.size())
            electrum->getBalance(0, callback);
        else
            mainReactor->stop();
    };

    electrum->getBalance(0, callback);
    mainReactor->run();
}

void testReconnectToInvalidAddresses()
{
    std::cout << "\nTesting connection to invalid addresses (automatic node selection mode is turned ON)...\n";

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    bitcoin::ElectrumSettings settings;
    settings.m_automaticChooseAddress = true;
    settings.m_secretWords = { "child", "happy", "moment", "weird", "ten", "token", "stuff", "surface", "success", "desk", "embark", "observe" };
    settings.m_nodeAddresses = {
        "tutututu1",
        "tutututu2",
        "tutututu3",
        "tutututu4"
    };

    auto provider = std::make_shared<bitcoin::Provider>(settings);
    auto electrum = std::make_shared<bitcoin::Electrum>(*mainReactor, *provider);
    std::size_t ind = 0;
    std::function<void()> nextStep;
    auto callback = [&](const bitcoin::IBridge::Error& error, uint64_t)
    {
        ind++;
        WALLET_CHECK(error.m_type == bitcoin::IBridge::IOError);
        if (ind < settings.m_nodeAddresses.size())
        {
            WALLET_CHECK(provider->GetSettings().GetElectrumConnectionOptions().m_address == settings.m_nodeAddresses[ind]);
        }
        else
        {
            WALLET_CHECK(provider->GetSettings().GetElectrumConnectionOptions().m_address == settings.m_nodeAddresses.front());
        }

        nextStep();
    };

    nextStep = [&]()
    {
        if (ind < settings.m_nodeAddresses.size())
            electrum->getBalance(0, callback);
        else
            mainReactor->stop();
    };

    electrum->getBalance(0, callback);
    mainReactor->run();
}

int main()
{
    int logLevel = LOG_LEVEL_DEBUG;
    auto logger = beam::Logger::create(logLevel, logLevel);

    testAddress();
    //testConnection();
    testReconnect1();
    testReconnect2();
    testConnectToOfflineNode();
    testConnectToInvalidAddress();
    testReconnectToInvalidAddresses();
    
    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}