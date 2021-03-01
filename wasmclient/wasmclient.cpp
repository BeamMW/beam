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

    void StartWallet()
    {
        m_Client.getAsync()->enableBodyRequests(true);
        m_Client.start({}, true, {});
    }

    void Send(const std::string& receiver, int amount, int fee)
    {
        WalletID w;
        w.FromHex(receiver);
        m_Client.getAsync()->sendMoney(w, "", (Amount)amount, (Amount)fee);
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


// Binding code
EMSCRIPTEN_BINDINGS() 
{
    class_<WasmWalletClient>("WasmWalletClient")
        .constructor<const std::string&, const std::string&, const std::string&>()
        .function("startWallet",                &WasmWalletClient::StartWallet)
        .function("send",                       &WasmWalletClient::Send)
        ;
}