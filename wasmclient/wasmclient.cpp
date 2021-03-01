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
#include <emscripten/threading.h>
#include <emscripten/val.h>

#include "wallet/client/wallet_client.h"
#include "mnemonic/mnemonic.h"
#include "utility/string_helpers.h"
#include <boost/algorithm/string.hpp>

#include <queue>
#include <exception>


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

class WalletClient2 : public WalletClient
{
public:
    using WalletClient::WalletClient;
private:
    void onPostFunctionToClientContext(MessageFunction&& func)
    {
        LOG_DEBUG() << std::this_thread::get_id() << " onPostFunctionToClientContext";
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_Messages.push(std::move(func));
        }
        emscripten_async_run_in_main_runtime_thread(
            EM_FUNC_SIG_VI,
            &WalletClient2::ProsessMessageOnMainThread,
            reinterpret_cast<int>(this));
    }
    static void ProsessMessageOnMainThread(int pThis)
    {
        reinterpret_cast<WalletClient2*>(pThis)->ProsessMessageOnMainThread2();
    }

    void ProsessMessageOnMainThread2()
    {
        LOG_DEBUG() << std::this_thread::get_id() << "[Main thread] ProsessMessageOnMainThread";
        if (!m_Messages.empty())
        {
            auto func = m_Messages.front();
            func();
            m_Messages.pop();
        }
    }
public:
    void PushCallback(val&& callback)
    {
        m_Callbacks.push(std::move(callback));
    }
    val PopCallback()
    {
        if (m_Callbacks.empty())
        {
            throw std::runtime_error("unexpected pop");
        }
        auto cb = std::move(m_Callbacks.front());
        m_Callbacks.pop();
        return cb;
    }
private:
    std::mutex m_Mutex;
    std::queue<MessageFunction> m_Messages;
    std::queue<val> m_Callbacks;
};

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

    void GetMaxPrivacyLockTimeLimitHours(val callback)
    {
        m_Client.PushCallback(std::move(callback));
        m_Client.getAsync()->getMaxPrivacyLockTimeLimitHours([this](uint8_t v)
        {
            auto cb = m_Client.PopCallback();
            cb(v);
        });
    }

private:
    std::string m_Seed;
    std::shared_ptr<Logger> m_Logger;
    std::string m_DbName;
    std::mutex m_Mutex;
    io::Reactor::Ptr m_Reactor;
    IWalletDB::Ptr m_Db;
    WalletClient2 m_Client;
};


// Binding code
EMSCRIPTEN_BINDINGS() 
{
    class_<WasmWalletClient>("WasmWalletClient")
        .constructor<const std::string&, const std::string&, const std::string&>()
        .function("startWallet",                      &WasmWalletClient::StartWallet)
        .function("send",                             &WasmWalletClient::Send)
        .function("getMaxPrivacyLockTimeLimitHours",  &WasmWalletClient::GetMaxPrivacyLockTimeLimitHours)
        ;
}