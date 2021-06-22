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

#include <emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/threading.h>
#include <emscripten/val.h>

#include "wallet/client/wallet_client.h"
#include "wallet/core/wallet_db.h"
#include "wallet/api/i_wallet_api.h"
#include "wallet/transactions/lelantus/lelantus_reg_creators.h"
#include "wallet/transactions/lelantus/push_transaction.h"
#include "mnemonic/mnemonic.h"
#include "utility/string_helpers.h"

#include <boost/algorithm/string.hpp>

#include <queue>
#include <exception>
#include <filesystem>
namespace fs = std::filesystem;

using namespace beam;
using namespace beam::io;
using namespace std;

using namespace emscripten;
using namespace ECC;
using namespace beam::wallet;

namespace
{
    void GetWalletSeed(NoLeak<uintBig>& walletSeed, const std::string& s)
    {
        SecString seed;
        WordList phrase;

        auto tempPhrase = s;
        boost::algorithm::trim_if(tempPhrase, [](char ch) { return ch == ';'; });
        phrase = string_helpers::split(tempPhrase, ';');

        auto buf = decodeMnemonic(phrase);
        seed.assign(buf.data(), buf.size());

        walletSeed.V = seed.hash().V;
    }

    void GenerateDefaultAddress(IWalletDB::Ptr db)
    {
        db->generateAndSaveDefaultAddress();
    }

    IWalletDB::Ptr CreateDatabase(const std::string& s, const std::string& dbName, const std::string& pass)
    {
        ECC::NoLeak<ECC::uintBig> seed;
        GetWalletSeed(seed, s);
        auto r = io::Reactor::create();
        io::Reactor::Scope scope(*r);
        auto db = WalletDB::init(dbName, SecString(pass), seed);
        GenerateDefaultAddress(db);
        EM_ASM
        (
            FS.syncfs(false, function()
            {
                console.log("wallet created!");
            });
        );
        return db;
    }
}

class WalletClient2 
    : public WalletClient
    , public std::enable_shared_from_this<WalletClient2>
{
public:
    using Callback = std::function<void()>;
    using WeakPtr = std::weak_ptr<WalletClient2>;

    struct ICallbackHandler
    {
        virtual void OnSyncProgress(int done, int total) {}
        virtual void OnResult(const json&) {}
    };

    using WalletClient::WalletClient;

    virtual ~WalletClient2()
    {
        stopReactor(true);
    }

    void SetHandler(ICallbackHandler* cb)
    {
        m_CbHandler = cb;
    }

    void SendResult(const json& result)
    {
        postFunctionToClientContext([this, result]()
        {
            if (m_CbHandler)
            {
                m_CbHandler->OnResult(result);
            }
        });
    }

    void Stop(Callback&& handler)
    {
        m_StoppedHandler = std::move(handler);
        stopReactor(true);
    }

    IWalletDB::Ptr GetWalletDB()
    {
        return getWalletDB();
    }

private:
    void onSyncProgressUpdated(int done, int total) override
    {
        postFunctionToClientContext([this, done, total]()
        {
            if (m_CbHandler)
            {
                m_CbHandler->OnSyncProgress(done, total);
            }
        });
    }

    void onPostFunctionToClientContext(MessageFunction&& func) override
    {
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_Messages.push(std::move(func));
        }
        auto thisWeakPtr = std::make_unique<WeakPtr>(shared_from_this());
        emscripten_async_run_in_main_runtime_thread(
            EM_FUNC_SIG_VI,
            &WalletClient2::ProsessMessageOnMainThread,
            reinterpret_cast<int>(thisWeakPtr.release()));
    }

    void onStopped() override
    {
        postFunctionToClientContext([this]()
        {
            if (m_StoppedHandler)
            {
                m_StoppedHandler();
            }
        });
    }

    static void ProsessMessageOnMainThread(int pThis)
    {
        std::unique_ptr<WeakPtr> p(reinterpret_cast<WeakPtr*>(pThis));
        if (auto sp = p->lock())
        {
            sp->ProsessMessageOnMainThread2();
        }
    }

    void ProsessMessageOnMainThread2()
    {
        while (!m_Messages.empty())
        {
            auto& func = m_Messages.front();
            func();
            m_Messages.pop();
        }
    }

private:
    std::mutex m_Mutex;
    std::queue<MessageFunction> m_Messages;
    ICallbackHandler* m_CbHandler = nullptr;
    Callback m_StoppedHandler;
    IWalletApi::Ptr m_WalletApi;
};

class WasmWalletClient 
    : public IWalletApiHandler
    , private WalletClient2::ICallbackHandler
{
public:
    WasmWalletClient(const std::string& dbName, const std::string& pass, const std::string& node)
        : m_Logger(beam::Logger::create(LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG))
        , m_Reactor(io::Reactor::create())
        , m_DbPath(dbName)
        , m_Pass(pass)
        , m_Node(node)
    {
        wallet::g_AssetsEnabled = true;
    }

    void sendAPIResponse(const json& result) override
    {
        m_Client->SendResult(result);
    }

    uint32_t AddCallback(val&& callback)
    {
        for (uint32_t i = 0; i < m_Callbacks.size(); ++i)
        {
            auto& cb = m_Callbacks[i];
            if (cb.isNull())
            {
                cb = std::move(callback);
                return i;
            }
        }
        m_Callbacks.push_back(std::move(callback));
        return static_cast<uint32_t>(m_Callbacks.size() - 1);
    }

    int Subscribe(val callback)
    {
        for (uint32_t i = 0; i < m_Callbacks.size(); ++i)
        {
            auto& cb = m_Callbacks[i];
            if (cb.isNull())
            {
                cb = std::move(callback);
                return i;
            }
        }
        m_Callbacks.push_back(std::move(callback));
        return static_cast<uint32_t>(m_Callbacks.size() - 1);
    }

    void Unsubscribe(int key)
    {
        if (key == m_Callbacks.size() - 1)
        {
            m_Callbacks.pop_back();
        }
        else if (key < m_Callbacks.size() - 1)
        {
            m_Callbacks[key] = val::null();
        }
    }

    void SetSyncHandler(val handler)
    {
        m_SyncHandler = std::make_unique<val>(handler);
    }

    void ExecuteAPIRequest(const std::string& request)
    {
        if (!m_Client)
        { 
            LOG_ERROR() << "Client is not running";
            return;
        }
        m_Client->getAsync()->makeIWTCall([this, request]()
        {
            if (!m_WalletApi)
            {
                IWalletApi::InitData initData;
                initData.walletDB = m_Client->GetWalletDB();
                initData.wallet = m_Client->getWallet();
                // initData.appID = set value
                // initData.swaps = _swapsProvider;
                // initData.acl = _acl;
                // initData.contracts = m_Client->getAppsShaders(); // for now blocked
                m_WalletApi = IWalletApi::CreateInstance(ApiVerCurrent, *this, initData);
            }

            m_WalletApi->executeAPIRequest(request.data(), request.size());
            return boost::none;
        }, 
        [](const boost::any&) {
        });
    }

    bool StartWallet()
    {
        if (m_Client)
        {
            LOG_WARNING() << "The client is already running";
            return false;
        }

        try
        {
            auto db = OpenWallet(m_DbPath, m_Pass);
            m_Client = std::make_shared<WalletClient2>(Rules::get(), db, m_Node, m_Reactor);
            m_Client->SetHandler(this);
            auto additionalTxCreators = std::make_shared<std::unordered_map<TxType, BaseTransaction::Creator::Ptr>>();
            additionalTxCreators->emplace(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>(db));
            m_Client->getAsync()->enableBodyRequests(true);
            m_Client->start({}, true, additionalTxCreators);
            return true;
        }
        catch (const std::exception& ex)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << ex.what();
        }
        return false;
    }

    void StopWallet(val handler = val::null())
    {
        m_Client->Stop([this, handler]() 
        {
            val handlerCopy = handler;
            m_Client.reset();
            m_WalletApi.reset();
            if (!handlerCopy.isNull())
            {
                
                auto handlerPtr = std::make_unique<val>(std::move(handlerCopy));
                emscripten_dispatch_to_thread_async(
                    get_thread_id(),
                    EM_FUNC_SIG_VI,
                    &WasmWalletClient::DoCallbackOnMainThread,
                    nullptr,
                    handlerPtr.release());
            }

        });
    }

    void OnSyncProgress(int done, int total) override
    {
        if (m_SyncHandler && !m_SyncHandler->isNull())
        {
            (*m_SyncHandler)(done, total);
        }
    }

    void OnResult(const json& result) override
    {
        auto r = result.dump();
        for (auto& cb : m_Callbacks)
        {
            if (!cb.isNull())
            {
                cb(r);
            }
        }
    }

    static void DoCallbackOnMainThread(val* h)
    {
        std::unique_ptr<val> handler(h);
        if (!handler->isNull())
        {
            (*handler)();
        }
        
    }

    bool IsRunning() const
    {
        return m_Client->isRunning();
    }

    static std::string GeneratePhrase()
    {
        return boost::join(createMnemonic(getEntropy()), " ");
    }

    static bool IsAllowedWord(const std::string& word)
    {
        return isAllowedWord(word);
    }

    static bool IsValidPhrase(const std::string& words)
    {
        return isValidMnemonic(string_helpers::split(words, ' '));
    }

    static std::string ConvertTokenToJson(const std::string& token)
    {
        return wallet::ConvertTokenToJson(token);
    }

    static std::string ConvertJsonToToken(const std::string& jsonParams)
    {
        return wallet::ConvertJsonToToken(jsonParams);
    }

    static void CreateWallet(const std::string& seed, const std::string& dbName, const std::string& pass)
    {
        CreateDatabase(seed, dbName, pass);
    }

    static void DeleteWallet(const std::string& dbName)
    {
        fs::remove(dbName);
        //EM_ASM
        //(
        //    FS.syncfs(false, function()
        //    {
        //        console.log("wallet deleted!");
        //    });
        //);
    }

    static IWalletDB::Ptr OpenWallet(const std::string& dbName, const std::string& pass)
    {
        Rules::get().UpdateChecksum();
        LOG_INFO() << "Rules signature: " << Rules::get().get_SignatureStr();
        return WalletDB::open(dbName, SecString(pass));
    }

    static void MountFS(val cb)
    {
        m_MountCB = cb;
        EM_ASM
        (
            {
                FS.mkdir("/beam_wallet");
                FS.mount(IDBFS, {}, "/beam_wallet");
                console.log("mounting...");
                FS.syncfs(true, function()
                {
                    console.log("mounted");
                    dynCall('v', $0);
                });

            }, OnMountFS
        );
    }
private:
    static void OnMountFS()
    {
        m_MountCB();
    }

private:
    static val m_MountCB;
    std::shared_ptr<Logger> m_Logger;
    io::Reactor::Ptr m_Reactor;
    std::string m_DbPath;
    std::string m_Pass;
    std::string m_Node;
    std::vector<val> m_Callbacks;
    std::unique_ptr<val> m_SyncHandler;
    std::shared_ptr<WalletClient2> m_Client;
    IWalletApi::Ptr m_WalletApi;
};

val WasmWalletClient::m_MountCB = val::null();

// Binding code
EMSCRIPTEN_BINDINGS() 
{
    class_<WasmWalletClient>("WasmWalletClient")
        .constructor<const std::string&, const std::string&, const std::string&>()
        .function("startWallet",                     &WasmWalletClient::StartWallet)
        .function("stopWallet",                      &WasmWalletClient::StopWallet)
        .function("isRunning",                       &WasmWalletClient::IsRunning)
        .function("sendRequest",                     &WasmWalletClient::ExecuteAPIRequest)
        .function("subscribe",                       &WasmWalletClient::Subscribe)
        .function("unsubscribe",                     &WasmWalletClient::Unsubscribe)
        .function("setSyncHandler",                  &WasmWalletClient::SetSyncHandler)
        .class_function("GeneratePhrase",            &WasmWalletClient::GeneratePhrase)
        .class_function("IsAllowedWord",             &WasmWalletClient::IsAllowedWord)
        .class_function("IsValidPhrase",             &WasmWalletClient::IsValidPhrase)
        .class_function("ConvertTokenToJson",        &WasmWalletClient::ConvertTokenToJson)
        .class_function("ConvertJsonToToken",        &WasmWalletClient::ConvertJsonToToken)
        .class_function("CreateWallet",              &WasmWalletClient::CreateWallet)
        .class_function("MountFS",                   &WasmWalletClient::MountFS)
        .class_function("DeleteWallet",              &WasmWalletClient::DeleteWallet)
    ;
}