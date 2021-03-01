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
        : m_Logger(beam::Logger::create(LOG_LEVEL_VERBOSE, LOG_LEVEL_VERBOSE))
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

    // proxies for IWalletModelAsync methods
//    void sendMoney(const WalletID& receiver, const std::string& comment, Amount amount, Amount fee = 0) = 0;
//    void sendMoney(const WalletID& sender, const WalletID& receiver, const std::string& comment, Amount amount, Amount fee = 0) = 0;
//    void startTransaction(TxParameters&& parameters) = 0;
//    void syncWithNode() = 0;
//    void calcChange(Amount amount, Amount fee, Asset::ID assetId) = 0;
//    void calcShieldedCoinSelectionInfo(Amount amount, Amount beforehandMinFee, Asset::ID assetId, bool isShielded = false) = 0;
//    void getWalletStatus() = 0;
//    void getTransactions() = 0;
//    void getUtxosStatus(beam::Asset::ID) = 0;
//    void getAddresses(bool own) = 0;
    void GetAddresses(bool own, val cb)
    {
        m_Client.PushCallback(std::move(cb));
        m_Client.getAsync()->getAddresses(own, [this](const std::vector<WalletAddress>& v)
        {
            auto cb = m_Client.PopCallback();
            cb(v);
        });
    }
//    void cancelTx(const TxID& id) = 0;
//    void deleteTx(const TxID& id) = 0;
//    void getCoinsByTx(const TxID& txId) = 0;
//    void saveAddress(const WalletAddress& address, bool bOwn) = 0;
//    void generateNewAddress() = 0;
//    void generateNewAddress(AsyncCallback<const WalletAddress&>&& callback) = 0;
//#ifdef BEAM_ATOMIC_SWAP_SUPPORT
//    void loadSwapParams() = 0;
//    void storeSwapParams(const beam::ByteBuffer& params) = 0;
//    void getSwapOffers() = 0;
//    void publishSwapOffer(const SwapOffer& offer) = 0;
//#endif  // BEAM_ATOMIC_SWAP_SUPPORT
//    void getDexOrders() = 0;
//    void publishDexOrder(const DexOrder&) = 0;
//    // TODO:DEX this is only for test, if will remain consider replacing QString to actual type
//    void acceptDexOrder(const DexOrderID&) = 0;
//    void deleteAddress(const WalletID& id) = 0;
//    void deleteAddress(const std::string& addr) = 0;
//    void updateAddress(const WalletID& id, const std::string& name, WalletAddress::ExpirationStatus status) = 0;
//    void activateAddress(const WalletID& id) = 0;
//    void getAddress(const WalletID& id) = 0;
//    void getAddress(const WalletID& id, AsyncCallback<const boost::optional<WalletAddress>&, size_t>&& callback) = 0;
//    void getAddress(const std::string& addr, AsyncCallback<const boost::optional<WalletAddress>&, size_t>&& callback) = 0;
//    void saveVouchers(const ShieldedVoucherList& v, const WalletID& walletID) = 0;
//    void setNodeAddress(const std::string& addr) = 0;
//    void changeWalletPassword(const beam::SecString& password) = 0;
//    void getNetworkStatus() = 0;
//    void rescan() = 0;
//    void exportPaymentProof(const TxID& id) = 0;
//    void checkAddress(const std::string& addr) = 0;
//    void importRecovery(const std::string& path) = 0;
//    void importDataFromJson(const std::string& data) = 0;
//    void exportDataToJson() = 0;
//    void exportTxHistoryToCsv() = 0;
//    void switchOnOffExchangeRates(bool isActive) = 0;
//    void switchOnOffNotifications(Notification::Type type, bool isActive) = 0;
//    void getNotifications() = 0;
//    void markNotificationAsRead(const ECC::uintBig& id) = 0;
//    void deleteNotification(const ECC::uintBig& id) = 0;
//    void getExchangeRates() = 0;
//    void getPublicAddress() = 0;
//    void generateVouchers(uint64_t ownID, size_t count, AsyncCallback<ShieldedVoucherList>&& callback) = 0;
//    void getAssetInfo(Asset::ID) = 0;
//    void makeIWTCall(std::function<boost::any()>&& function, AsyncCallback<boost::any>&& resultCallback) = 0;
//    // error (if any), shader output (if any), txid (if any)
//    typedef AsyncCallback<const std::string&, const std::string&, const TxID&> ShaderCallback;
//    void callShader(const std::vector<uint8_t>& shader, const std::string& args, ShaderCallback&& cback) = 0;
//    void setMaxPrivacyLockTimeLimitHours(uint8_t limit) = 0;
//    void getMaxPrivacyLockTimeLimitHours(AsyncCallback<uint8_t>&& callback) = 0;
//    void getCoins(Asset::ID assetId, AsyncCallback<std::vector<Coin>>&& callback) = 0;
//    void getShieldedCoins(Asset::ID assetId, AsyncCallback<std::vector<ShieldedCoin>>&& callback) = 0;
//    void enableBodyRequests(bool value) = 0;

private:
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
    register_vector<WalletAddress>("vector<WalletAddress>");

    class_<PeerID>("PeerID")
        .constructor<>()
        .function("str",          &PeerID::str)
        ;
    //value_object<std::array<uint8_t, 8>>("uintBigFor<BbsChannel>::Type");

    //value_object<WalletID>("WalletID")
    //    .field("channel",       &WalletID::m_Channel)
    //    .field("publicKey",     &WalletID::m_Pk)
    //    ;


    value_object<WalletAddress>("WalletAddress")
        //.field("walletID",      &WalletAddress::m_walletID)
        .field("label",         &WalletAddress::m_label)
        .field("category",      &WalletAddress::m_category)
        //.field("createTime",    &WalletAddress::m_createTime)
        //.field("duration",      &WalletAddress::m_duration)
        //.field("ownID",         &WalletAddress::m_OwnID)
        .field("identity",      &WalletAddress::m_Identity)
        .field("address",       &WalletAddress::m_Address)
        ;

    class_<WasmWalletClient>("WasmWalletClient")
        .constructor<const std::string&, const std::string&, const std::string&>()
        .function("startWallet",                     &WasmWalletClient::StartWallet)
        .function("send",                            &WasmWalletClient::Send)
        .function("getMaxPrivacyLockTimeLimitHours", &WasmWalletClient::GetMaxPrivacyLockTimeLimitHours)
        .function("getAddresses",                    &WasmWalletClient::GetAddresses)
    ;
}