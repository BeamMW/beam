// Copyright 2021 The Beam Team
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

// #include "wallet/client/wallet_client.h"
#include <emscripten/bind.h>
#include <string>
#include <thread>
#include <sstream>
#include <chrono>
#include <vector>
#include <mutex>
#include <memory>
#include "wallet/client/wallet_client.h"
#include "mnemonic/mnemonic.h"
#include "utility/string_helpers.h"
#include <boost/algorithm/string.hpp>

#include <stdio.h>
#include <stdlib.h>


using namespace beam;
using namespace beam::io;
using namespace std;

using namespace emscripten;
using namespace ECC;
using namespace beam::wallet;

namespace
{
    bool GetWalletSeed(NoLeak<uintBig>& walletSeed, const std::string& s)
    {
        SecString seed;
        WordList phrase;

        auto tempPhrase = s;
        boost::algorithm::trim_if(tempPhrase, [](char ch) { return ch == ';'; });
        phrase = string_helpers::split(tempPhrase, ';');

        auto buf = decodeMnemonic(phrase);
        seed.assign(buf.data(), buf.size());

        walletSeed.V = seed.hash().V;
        return true;
    }

    void GenerateDefaultAddress(IWalletDB::Ptr db)
    {
        // generate default address
        WalletAddress address;
        db->createAddress(address);
        address.m_label = "default";
        db->saveAddress(address);
        LOG_DEBUG() << "Default address: " << std::to_string(address.m_walletID);
    }

    IWalletDB::Ptr CreateDatabase(const std::string& s, const std::string& dbName, io::Reactor::Ptr r)
    {
        Rules::get().UpdateChecksum();
        LOG_INFO() << "Rules signature: " << Rules::get().get_SignatureStr();
        ECC::NoLeak<ECC::uintBig> seed;
        GetWalletSeed(seed, s);
        puts("TestWalletDB...");
        io::Reactor::Scope scope(*r);
        auto db = WalletDB::init(dbName, std::string("123"), seed);
        GenerateDefaultAddress(db);
        return db;
    }
}

class WasmWalletClient //: public WalletClient
{
public:
    WasmWalletClient(const std::string& s, const std::string& dbName, const std::string& node)
        : m_Seed(s)
        , m_Logger(beam::Logger::create(LOG_LEVEL_VERBOSE, LOG_LEVEL_VERBOSE))
        , m_DbName(dbName)
        , m_Reactor(io::Reactor::create())
        , m_Db(CreateDatabase(s, dbName, m_Reactor))
        , m_Client(Rules::get(), m_Db, node, m_Reactor)
    {}

    std::string TestThreads()
    {
        std::vector<std::thread> threads;

        for (int i = 0; i < 5; ++i)
        {
            threads.emplace_back([this]() { ThreadFunc(); });
        }

        for (auto& t : threads)
        {
            if (t.joinable())
            {
                t.join();
            }
        }

        return "TestThreads " + std::to_string(std::thread::hardware_concurrency()) + m_Seed;
    }

    void ThreadFunc()
    {
        std::stringstream ss;
        ss << "\nThread #" << std::this_thread::get_id();
        {
            std::unique_lock lock(m_Mutex);
            m_Seed += ss.str();
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::string TestWalletDB()
    {
        ECC::NoLeak<ECC::uintBig> seed;
        seed.V = 10283UL;
        puts("TestWalletDB...");
        auto reactor = io::Reactor::create();
        io::Reactor::Scope scope(*reactor);
        auto walletDB = WalletDB::init("//test_wallet.db", std::string("123"), seed);
        if (walletDB)
        {
            puts("setting new state...");
            beam::Block::SystemState::ID id = { };
            id.m_Height = 134;
            walletDB->setSystemStateID(id);
            return std::to_string(walletDB->getCurrentHeight());
        }
        else
        {
            puts("failed to open");
            return "";
        }
    }

    void TestReactor()
    {
    
            Reactor::Ptr reactor = Reactor::create();
          
            auto f = std::async(
                std::launch::async,
                [reactor]() {
                this_thread::sleep_for(chrono::microseconds(300000));
                //usleep(300000);
                LOG_DEBUG() << "stopping reactor from foreign thread...";
                reactor->stop();
            }
            );
          
            LOG_DEBUG() << "starting reactor...";;
            reactor->run();
            LOG_DEBUG() << "reactor stopped";
          
            f.get();
    }

    void StartWallet()
    {
        m_Client.getAsync()->enableBodyRequests(true);
        m_Client.start({}, true, {});
    }

private:
    std::string m_Seed;
    std::shared_ptr<Logger> m_Logger;
    std::string m_DbName;
    std::mutex m_Mutex;
    io::Reactor::Ptr m_Reactor;
    IWalletDB::Ptr m_Db;
    WalletClient m_Client;
};

//struct WasmClientWrapper
//{
//public:
//    WasmClientWrapper(const std::string& phrase)
//    {
//        _client = make_unique<WalletModel>(walletDB, "127.0.0.1:10005", reactor);
//    }
//
//private:
//    std::unique_ptr<WasmClient> _client;
//
//};
// Binding code
EMSCRIPTEN_BINDINGS() 
{
    class_<WasmWalletClient>("WasmWalletClient")
        .constructor<const std::string&, const std::string&, const std::string&>()
        .function("testThreads",                &WasmWalletClient::TestThreads)
        //.function("testOpenSSL",                &WasmWalletClient::TestOpenSSL)
        .function("testReactor",                &WasmWalletClient::TestReactor)
        .function("testWalletDB",               &WasmWalletClient::TestWalletDB)
        .function("startWallet",                &WasmWalletClient::StartWallet)
 
        ;
}