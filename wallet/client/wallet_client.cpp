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

#include "wallet_client.h"
#include "wallet/core/simple_transaction.h"
#include "wallet/transactions/dex/dex_tx.h"
#include "utility/log_rotation.h"
#include "http/http_client.h"
#include "core/block_rw.h"
#include "wallet/core/common_utils.h"
#include "extensions/broadcast_gateway/broadcast_router.h"
#include "extensions/export/tx_history_to_csv.h"
#include "extensions/news_channels/wallet_updates_provider.h"
#include "extensions/news_channels/exchange_rate_provider.h"
#include "utility/fsutils.h"
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
#include "wallet/client/extensions/offers_board/swap_offers_board.h"
#endif  // BEAM_ATOMIC_SWAP_SUPPORT
#ifdef BEAM_LELANTUS_SUPPORT
#include "wallet/transactions/lelantus/push_transaction.h"
#endif // BEAM_LELANTUS_SUPPORT

#include "filter.h"
#include <regex>

using namespace std;

namespace
{
using namespace beam;
using namespace beam::wallet;

constexpr size_t kCollectorBufferSize = 100;
constexpr size_t kShieldedPer24hFilterSize = 20;
constexpr size_t kShieldedPer24hFilterBlocksForUpdate = 144;
constexpr size_t kShieldedCountHistoryWindowSize = kShieldedPer24hFilterSize << 1;
constexpr int kOneTimeLoadTxCount = 100;

using WalletSubscriber = ScopedSubscriber<wallet::IWalletObserver, wallet::Wallet>;

struct WalletModelBridge : public Bridge<IWalletModelAsync>
{
    BRIDGE_INIT(WalletModelBridge);

    void sendMoney(const wallet::WalletID& receiverID, const std::string& comment, Amount amount, Amount fee) override
    {
        typedef void(IWalletModelAsync::*SendMoneyType)(const wallet::WalletID&, const std::string&, Amount, Amount);
        call_async((SendMoneyType)&IWalletModelAsync::sendMoney, receiverID, comment, amount, fee);
    }

    void sendMoney(const wallet::WalletID& senderID, const wallet::WalletID& receiverID, const std::string& comment, Amount amount, Amount fee) override
    {
        typedef void(IWalletModelAsync::*SendMoneyType)(const wallet::WalletID &, const wallet::WalletID &, const std::string &, Amount, Amount);
        call_async((SendMoneyType)&IWalletModelAsync::sendMoney, senderID, receiverID, comment, amount, fee);
    }

    void startTransaction(TxParameters&& parameters) override
    {
        call_async(&IWalletModelAsync::startTransaction, move(parameters));
    }

    void syncWithNode() override
    {
        call_async(&IWalletModelAsync::syncWithNode);
    }

    void calcChange(Amount amount, Amount fee, Asset::ID assetId) override
    {
        call_async(&IWalletModelAsync::calcChange, amount, fee, assetId);
    }

    void selectCoins(Amount amount, Amount beforehandMinFee, Asset::ID assetId, bool isShielded /* = false */) override
    {
        typedef void(IWalletModelAsync::* MethodType)(Amount, Amount, Asset::ID, bool);
        call_async((MethodType)&IWalletModelAsync::selectCoins, amount, beforehandMinFee, assetId, isShielded);
    }

    void selectCoins(Amount amount, Amount beforehandMinFee, Asset::ID assetId, bool isShielded, AsyncCallback<const CoinsSelectionInfo&>&& callback) override
    {
        typedef void(IWalletModelAsync::* MethodType)(Amount, Amount, Asset::ID, bool, AsyncCallback<const CoinsSelectionInfo&>&&);
        call_async((MethodType)&IWalletModelAsync::selectCoins, amount, beforehandMinFee, assetId, isShielded, callback);
    }

    void getWalletStatus() override
    {
        call_async(&IWalletModelAsync::getWalletStatus);
    }

    void getTransactions() override
    {
        typedef void(IWalletModelAsync::* MethodType)();
        call_async((MethodType)&IWalletModelAsync::getTransactions);
    }

    void getTransactions(AsyncCallback<const std::vector<TxDescription>&>&& callback) override
    {
        typedef void(IWalletModelAsync::* MethodType)(AsyncCallback<const std::vector<TxDescription>&>&&);
        call_async((MethodType)&IWalletModelAsync::getTransactions, callback);
    }

    void getTransactionsSmoothly() override
    {
        typedef void(IWalletModelAsync::* MethodType)();
        call_async((MethodType)&IWalletModelAsync::getTransactionsSmoothly);
    }

    void getAllUtxosStatus() override
    {
        call_async(&IWalletModelAsync::getAllUtxosStatus);
    }

    void getAddresses(bool own) override
    {
        typedef void(IWalletModelAsync::* MethodType)(bool);
        call_async((MethodType)&IWalletModelAsync::getAddresses, own);
    }

     void getDexOrders() override
     {
        call_async(&IWalletModelAsync::getDexOrders);
     }

     void publishDexOrder(const DexOrder& order) override
     {
        call_async(&IWalletModelAsync::publishDexOrder, order);
     }

     void acceptDexOrder(const DexOrderID& orderId) override
     {
        call_async(&IWalletModelAsync::acceptDexOrder, orderId);
     }

    void getAssetSwapOrders() override
    {
        call_async(&IWalletModelAsync::getAssetSwapOrders);
    }

    void publishAssetSwapOrder(const AssetSwapOrder& order) override
    {
        call_async(&IWalletModelAsync::publishAssetSwapOrder, order);
    }

    void acceptAssetSwapOrder(const DexOrderID& orderId) override
    {
        call_async(&IWalletModelAsync::acceptAssetSwapOrder, orderId);
    }

#ifdef BEAM_ATOMIC_SWAP_SUPPORT    
    void getSwapOffers() override
    {
		call_async(&IWalletModelAsync::getSwapOffers);
    }

    void publishSwapOffer(const wallet::SwapOffer& offer) override
    {
		call_async(&IWalletModelAsync::publishSwapOffer, offer);
    }

    void loadSwapParams() override
    {
        call_async(&IWalletModelAsync::loadSwapParams);
    }

    void storeSwapParams(const beam::ByteBuffer& params) override
    {
        call_async(&IWalletModelAsync::storeSwapParams, params);
    }
#endif

    void loadAssetSwapParams() override
    {
        call_async(&IWalletModelAsync::loadAssetSwapParams);
    }

    void storeAssetSwapParams(const beam::ByteBuffer& params) override
    {
        call_async(&IWalletModelAsync::storeAssetSwapParams, params);
    }

    void cancelTx(const wallet::TxID& id) override
    {
        call_async(&IWalletModelAsync::cancelTx, id);
    }

    void deleteTx(const wallet::TxID& id) override
    {
        call_async(&IWalletModelAsync::deleteTx, id);
    }

    void getCoinsByTx(const wallet::TxID& id) override
    {
        call_async(&IWalletModelAsync::getCoinsByTx, id);
    }

    void saveAddress(const wallet::WalletAddress& address) override
    {
        call_async(&IWalletModelAsync::saveAddress, address);
    }

    void generateNewAddress() override
    {
        typedef void(IWalletModelAsync::* MethodType)();
        call_async((MethodType)&IWalletModelAsync::generateNewAddress);
    }

    void generateNewAddress(AsyncCallback<const WalletAddress&>&& callback) override
    {
        typedef void(IWalletModelAsync::* MethodType)(AsyncCallback<const WalletAddress&>&&);
        call_async((MethodType)&IWalletModelAsync::generateNewAddress, std::move(callback));
    }

    void deleteAddress(const WalletID& addr) override
    {
        call_async(&IWalletModelAsync::deleteAddress, addr);
    }

    void updateAddress(const WalletID& address, const std::string& name, beam::Timestamp expirationTime) override
    {
        typedef void(IWalletModelAsync::* MethodType)(const WalletID&, const std::string&, beam::Timestamp);
        call_async((MethodType)&IWalletModelAsync::updateAddress, address, name, expirationTime);
    }

     void updateAddress(const WalletID& address, const std::string& name, WalletAddress::ExpirationStatus expirationStatus) override
     {
        typedef void(IWalletModelAsync::* MethodType)(const WalletID&, const std::string&, WalletAddress::ExpirationStatus);
        call_async((MethodType)&IWalletModelAsync::updateAddress, address, name, expirationStatus);
     }

    void activateAddress(const WalletID& address) override
    {
        call_async(&IWalletModelAsync::activateAddress, address);
    }

    void getAddress(const WalletID& id) override
    {
        typedef void(IWalletModelAsync::* MethodType)(const WalletID&);
        call_async((MethodType)&IWalletModelAsync::getAddress, id);
    }

    void getAddress(const WalletID& addr, AsyncCallback <const boost::optional<WalletAddress>&, size_t>&& callback) override
    {
        typedef void(IWalletModelAsync::* MethodType)(const WalletID&, AsyncCallback<const boost::optional<WalletAddress>&, size_t>&&);
        call_async((MethodType)&IWalletModelAsync::getAddress, addr, std::move(callback));
    }

    void getAddressByToken(const std::string& token, AsyncCallback <const boost::optional<WalletAddress>&, size_t>&& callback) override
    {
        call_async(&IWalletModelAsync::getAddressByToken, token, std::move(callback));
    }

    void deleteAddressByToken(const std::string& token) override
    {
        call_async(&IWalletModelAsync::deleteAddressByToken, token);
    }

    void saveVouchers(const ShieldedVoucherList& v, const WalletID& walletID) override
    {
        call_async(&IWalletModelAsync::saveVouchers, v, walletID);
    }

    void setNodeAddress(const std::string& addr) override
    {
        call_async(&IWalletModelAsync::setNodeAddress, addr);
    }

    #ifdef BEAM_IPFS_SUPPORT
    void setIPFSConfig(asio_ipfs::config&& cfg) override
    {
        call_async(&IWalletModelAsync::setIPFSConfig, std::move(cfg));
    }

    void stopIPFSNode() override
    {
        call_async(&IWalletModelAsync::stopIPFSNode);
    }

    void startIPFSNode() override
    {
        call_async(&IWalletModelAsync::startIPFSNode);
    }
    #endif

    void changeWalletPassword(const SecString& pass) override
    {
        // TODO: should be investigated, don't know how to "move" SecString into lambda
        std::string passStr(pass.data(), pass.size());

        call_async(&IWalletModelAsync::changeWalletPassword, passStr);
    }

    void getNetworkStatus() override
    {
        call_async(&IWalletModelAsync::getNetworkStatus);
    }

    #ifdef BEAM_IPFS_SUPPORT
    void getIPFSStatus() override
    {
        call_async(&IWalletModelAsync::getIPFSStatus);
    }
    #endif

    void rescan() override
    {
        call_async(&IWalletModelAsync::rescan);
    }

    void exportPaymentProof(const wallet::TxID& id) override
    {
        call_async(&IWalletModelAsync::exportPaymentProof, id);
    }

    void checkNetworkAddress(const std::string& addr) override
    {
        call_async(&IWalletModelAsync::checkNetworkAddress, addr);
    }

    void importRecovery(const std::string& path) override
    {
        call_async(&IWalletModelAsync::importRecovery, path);
    }

    void importDataFromJson(const std::string& data) override
    {
        call_async(&IWalletModelAsync::importDataFromJson, data);
    }

    void exportDataToJson() override
    {
        call_async(&IWalletModelAsync::exportDataToJson);
    }

    void exportTxHistoryToCsv() override
    {
        call_async(&IWalletModelAsync::exportTxHistoryToCsv);
    }

    void switchOnOffExchangeRates(bool isActive) override
    {
        call_async(&IWalletModelAsync::switchOnOffExchangeRates, isActive);
    }

    void switchOnOffNotifications(Notification::Type type, bool isActive) override
    {
        call_async(&IWalletModelAsync::switchOnOffNotifications, type, isActive);
    }
        
    void getNotifications() override
    {
        call_async(&IWalletModelAsync::getNotifications);
    }

    void markNotificationAsRead(const ECC::uintBig& id) override
    {
        call_async(&IWalletModelAsync::markNotificationAsRead, id);
    }

    void deleteNotification(const ECC::uintBig& id) override
    {
        call_async(&IWalletModelAsync::deleteNotification, id);
    }

    void getExchangeRates() override
    {
        call_async(&IWalletModelAsync::getExchangeRates);
    }

    void getVerificationInfo() override
    {
        call_async(&IWalletModelAsync::getVerificationInfo);
    }

    void getPublicAddress() override
    {
        call_async(&IWalletModelAsync::getPublicAddress);
    }

    void generateVouchers(uint64_t ownID, size_t count, AsyncCallback<const ShieldedVoucherList&>&& callback) override
    {
        call_async(&IWalletModelAsync::generateVouchers, ownID, count, std::move(callback));
    }

    void getAssetInfo(Asset::ID assetId) override
    {
        call_async(&IWalletModelAsync::getAssetInfo, assetId);
    }

    void makeIWTCall(std::function<boost::any()>&& function, AsyncCallback<const boost::any&>&& resultCallback) override
    {
        call_async(&IWalletModelAsync::makeIWTCall, std::move(function), std::move(resultCallback));
    }

    void callShader(std::vector<uint8_t>&& shader, std::string&& args, CallShaderCallback&& cback) override
    {
        typedef void(IWalletModelAsync::* MethodType)(std::vector<uint8_t>&&, std::string&&, CallShaderCallback&&);
        call_async((MethodType)&IWalletModelAsync::callShader, std::move(shader), std::move(args), std::move(cback));
    }

    void callShader(std::string&& shaderFile, std::string&& args, CallShaderCallback&& cback) override
    {
        typedef void(IWalletModelAsync::* MethodType)(std::string&&, std::string&&, CallShaderCallback&&);
        call_async((MethodType)&IWalletModelAsync::callShader, std::move(shaderFile), std::move(args), std::move(cback));
    }

    void callShaderAndStartTx(beam::ByteBuffer&& shader, std::string&& args, CallShaderAndStartTxCallback&& cback) override
    {
        typedef void(IWalletModelAsync::* MethodType)(std::vector<uint8_t>&&, std::string&&, CallShaderAndStartTxCallback&&);
        call_async((MethodType)&IWalletModelAsync::callShaderAndStartTx, std::move(shader), std::move(args), std::move(cback));
    }

    void callShaderAndStartTx(std::string&& shaderFile, std::string&& args, CallShaderAndStartTxCallback&& cback) override
    {
        typedef void(IWalletModelAsync::* MethodType)(std::string&&, std::string&&, CallShaderAndStartTxCallback&&);
        call_async((MethodType)&IWalletModelAsync::callShaderAndStartTx, std::move(shaderFile), std::move(args), std::move(cback));
    }

    void processShaderTxData(beam::ByteBuffer&& data, ProcessShaderTxDataCallback&& cback) override
    {
        call_async(&IWalletModelAsync::processShaderTxData, std::move(data), std::move(cback));
    }

    void setMaxPrivacyLockTimeLimitHours(uint8_t limit) override
    {
        call_async(&IWalletModelAsync::setMaxPrivacyLockTimeLimitHours, limit);
    }

    void getMaxPrivacyLockTimeLimitHours(AsyncCallback<uint8_t>&& callback) override
    {
        call_async(&IWalletModelAsync::getMaxPrivacyLockTimeLimitHours, std::move(callback));
    }

    void setCoinConfirmationsOffset(uint32_t limit) override
    {
        call_async(&IWalletModelAsync::setCoinConfirmationsOffset, limit);
    }

    void getCoinConfirmationsOffset(AsyncCallback<uint32_t>&& callback) override
    {
        call_async(&IWalletModelAsync::getCoinConfirmationsOffset, std::move(callback));
    }

    void removeRawSeedPhrase() override
    {
        call_async(&IWalletModelAsync::removeRawSeedPhrase);
    }

    void readRawSeedPhrase(AsyncCallback<const std::string&>&& callback) override
    {
        call_async(&IWalletModelAsync::readRawSeedPhrase, std::move(callback));
    }

    void getAppsList(AppsListCallback&& callback) override
    {
        call_async(&IWalletModelAsync::getAppsList, std::move(callback));
    }

    void markAppNotificationAsRead(const TxID& id) override
    {
        call_async(&IWalletModelAsync::markAppNotificationAsRead, id);
    }

    void enableBodyRequests(bool value) override
    {
        call_async(&IWalletModelAsync::enableBodyRequests, value);
    }
};
}

namespace beam::wallet
{
    bool WalletStatus::HasStatus(Asset::ID assetId) const
    {
        return all.find(assetId) != all.end();
    }

    WalletStatus::AssetStatus WalletStatus::GetStatus(Asset::ID assetId) const
    {
        if(all.find(assetId) == all.end())
        {
            AssetStatus result;
            return result;
        }
        return all[assetId];
    }

    WalletStatus::AssetStatus WalletStatus::GetBeamStatus() const
    {
        return GetStatus(Asset::s_BeamID);
    }

    WalletClient::WalletClient(const Rules& rules, IWalletDB::Ptr walletDB, OpenDBFunction&& walletDBFunc, const std::string& nodeAddr, io::Reactor::Ptr reactor)
        : m_rules(rules)
        , m_walletDB(walletDB)
        , m_reactor{ reactor ? reactor : io::Reactor::create() }
        , m_async{ make_shared<WalletModelBridge>(*(static_cast<IWalletModelAsync*>(this)), *m_reactor) }
        , m_connectedNodesCount(0)
        , m_trustedConnectionCount(0)
        , m_initialNodeAddrStr(nodeAddr)
        , m_CoinChangesCollector(kCollectorBufferSize, m_reactor, [this](auto action, const auto& items) { onNormalCoinsChanged(action, items); })
        , m_ShieldedCoinChangesCollector(kCollectorBufferSize, m_reactor, [this](auto action, const auto& items) { onShieldedCoinChanged(action, items); })
        , m_AddressChangesCollector(kCollectorBufferSize, m_reactor, [this](auto action, const auto& items) { onAddressesChanged(action, items); })
        , m_TransactionChangesCollector(kCollectorBufferSize, m_reactor, [this](auto action, const auto& items) {
             onTxStatus(action, items);
              })
        , m_shieldedPer24hFilter(std::make_unique<Filter>(kShieldedPer24hFilterSize))
        , m_openDBFunc(std::move(walletDBFunc))
    {
        m_ainfoDelayed = io::Timer::create(*m_reactor);
        m_balanceDelayed = io::Timer::create(*m_reactor);
    }

    WalletClient::WalletClient(const Rules& rules, IWalletDB::Ptr walletDB, const std::string& nodeAddr, io::Reactor::Ptr reactor)
        : WalletClient(rules, walletDB, {}, nodeAddr, reactor)
    {

    }

    WalletClient::WalletClient(const Rules& rules, OpenDBFunction&& walletDBFunc, const std::string& nodeAddr, io::Reactor::Ptr reactor)
        : WalletClient(rules, nullptr, std::move(walletDBFunc), nodeAddr, reactor)
    {
    }

    WalletClient::~WalletClient()
    {
        // reactor thread should be already stopped here, but just in case
        // this call is unsafe and may result in crash if reactor is not stopped
        assert(!m_thread);
        stopReactor();
    }

    void WalletClient::stopReactor(bool detachThread/* = false*/)
    {
        try
        {
            if (isRunning())
            {
                assert(m_reactor);
                m_reactor->stop();
                if (!detachThread)
                {
                    m_thread->join();
                }
                else
                {
                    m_thread->detach();
                }
            }
            m_thread.reset();
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...) {
            LOG_UNHANDLED_EXCEPTION();
        }
    }

    IWalletDB::Ptr WalletClient::getWalletDB()
    {
        return m_walletDB;
    }

    void WalletClient::postFunctionToClientContext(MessageFunction&& func)
    {
        onPostFunctionToClientContext(move(func));
    }
    //
    // UI thread. Methods below should be called from main thread
    //
    Version WalletClient::getLibVersion() const
    {
        // TODO: replace with current wallet library version
        return beam::Version
        {
            0,
            0,
            0
        };
    }

    uint32_t WalletClient::getClientRevision() const
    {
        return 0;
    }

    void WalletClient::start( std::map<Notification::Type,bool> activeNotifications,
                              bool withExchangeRates,
                              std::shared_ptr<std::unordered_map<TxType, BaseTransaction::Creator::Ptr>> txCreators)
    {
        if (isRunning())
        {
            return;
        }
        m_thread = std::make_shared<MyThread>([this, withExchangeRates, txCreators, activeNotifications]()
        {
            try
            {
                Rules::Scope scopeRules(getRules());
                io::Reactor::Scope scope(*m_reactor);
#ifndef __EMSCRIPTEN__
                static const unsigned LOG_ROTATION_PERIOD_SEC = 3 * 3600; // 3 hours
                static const unsigned LOG_CLEANUP_PERIOD_SEC = 120 * 3600; // 5 days
                LogRotation logRotation(*m_reactor, LOG_ROTATION_PERIOD_SEC, LOG_CLEANUP_PERIOD_SEC);
#endif // !__EMSCRIPTEN__

                if (!m_walletDB)
                {
                    if (m_openDBFunc)
                    {
                        m_walletDB = m_openDBFunc();
                    }
                    else
                    {
                        throw std::runtime_error("WalletClient: database is not provided");
                    }
                }

                auto wallet = make_shared<Wallet>(m_walletDB);
                m_wallet = wallet;

                if (txCreators)
                {
                    for (auto&[txType, creator] : *txCreators)
                    {
                        wallet->RegisterTransactionType(txType, creator);
                    }
                }

                wallet->ResumeAllTransactions();

                updateClientState(getStatus());

                std::vector<io::Address> fallbackAddresses;
                storage::getBlobVar(*m_walletDB, FallbackPeers, fallbackAddresses);
                auto nodeNetwork = make_shared<NodeNetwork>(*wallet, m_initialNodeAddrStr, std::move(fallbackAddresses));
                m_nodeNetwork = nodeNetwork;

                using NodeNetworkSubscriber = ScopedSubscriber<INodeConnectionObserver, NodeNetwork>;
                auto nodeNetworkSubscriber =
                    std::make_unique<NodeNetworkSubscriber>(static_cast<INodeConnectionObserver*>(this), nodeNetwork);

                auto walletNetwork = make_shared<WalletNetworkViaBbs>(*wallet, nodeNetwork, m_walletDB);
                m_walletNetwork = walletNetwork;
                wallet->SetNodeEndpoint(nodeNetwork);
                wallet->AddMessageEndpoint(walletNetwork);

                updateMaxPrivacyStatsImpl(getStatus());

                auto wallet_subscriber = make_unique<WalletSubscriber>(static_cast<IWalletObserver*>(this), wallet);

                // Notification center initialization
                m_notificationCenter =
                    make_shared<NotificationCenter>(*m_walletDB, activeNotifications, m_reactor->shared_from_this());
                using NotificationsSubscriber = ScopedSubscriber<INotificationsObserver, NotificationCenter>;

                struct MyNotificationsObserver : INotificationsObserver
                {
                    WalletClient& m_client;
                    explicit MyNotificationsObserver(WalletClient& client) : m_client(client) {}
                    void onNotificationsChanged(ChangeAction action, const std::vector<Notification>& items) override
                    {
                        m_client.updateNotifications();
                        static_cast<INotificationsObserver&>(m_client).onNotificationsChanged(action, items);
                    }
                } notificationObserver(*this);

                auto notificationsSubscriber =
                    make_unique<NotificationsSubscriber>(&notificationObserver, m_notificationCenter);
                updateNotifications();
                // Broadcast router and broadcast message consumers initialization
                auto broadcastRouter = make_shared<BroadcastRouter>(nodeNetwork, *walletNetwork, std::make_shared<BroadcastRouter::BbsTsHolder>(m_walletDB));
                m_broadcastRouter = broadcastRouter;

                using WalletDbSubscriber = ScopedSubscriber<IWalletDbObserver, IWalletDB>;
                // Swap offer board uses broadcasting messages
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
                OfferBoardProtocolHandler protocolHandler(m_walletDB->get_SbbsKdf());

                auto offersBulletinBoard = make_shared<SwapOffersBoard>(*broadcastRouter, protocolHandler, m_walletDB);
                m_offersBulletinBoard = offersBulletinBoard;

                using SwapOffersBoardSubscriber = ScopedSubscriber<ISwapOffersObserver, SwapOffersBoard>;

                auto walletDbSubscriber = make_unique<WalletDbSubscriber>(
                    static_cast<IWalletDbObserver*>(offersBulletinBoard.get()), m_walletDB);
                auto swapOffersBoardSubscriber = make_unique<SwapOffersBoardSubscriber>(
                    static_cast<ISwapOffersObserver*>(this), offersBulletinBoard);
#endif
                // Broadcast validator initialization. It verifies messages signatures.
                auto broadcastValidator = make_shared<BroadcastMsgValidator>();
                {
                    PeerID key;
                    if (BroadcastMsgValidator::stringToPublicKey(kBroadcastValidatorPublicKey, key))
                    {
                        broadcastValidator->setPublisherKeys( { key } );
                    }
                }

                // Other content providers using broadcast messages
                auto walletUpdatesProvider = make_shared<WalletUpdatesProvider>(*broadcastRouter, *broadcastValidator);
                auto exchangeRateProvider = make_shared<ExchangeRateProvider>(*broadcastRouter, *broadcastValidator, *m_walletDB, withExchangeRates);
                m_exchangeRateProvider = exchangeRateProvider;
                m_walletUpdatesProvider = walletUpdatesProvider;
                using WalletUpdatesSubscriber = ScopedSubscriber<INewsObserver, WalletUpdatesProvider>;
                using ExchangeRatesSubscriber = ScopedSubscriber<IExchangeRatesObserver, ExchangeRateProvider>;
                auto walletUpdatesSubscriber = make_unique<WalletUpdatesSubscriber>(static_cast<INewsObserver*>(m_notificationCenter.get()), walletUpdatesProvider);
                auto ratesSubscriber = make_unique<ExchangeRatesSubscriber>(static_cast<IExchangeRatesObserver*>(this), exchangeRateProvider);
                auto notificationsDbSubscriber = make_unique<WalletDbSubscriber>(static_cast<IWalletDbObserver*>(m_notificationCenter.get()), m_walletDB);

                //
                // Assets verification
                //
                auto verificationProvider = make_shared<VerificationProvider>(*broadcastRouter, *broadcastValidator, *m_walletDB);
                m_verificationProvider = verificationProvider;
                using VerificationSubscriber = ScopedSubscriber<IVerificationObserver, VerificationProvider>;
                auto verificationSubscriber = make_unique<VerificationSubscriber>(static_cast<IVerificationObserver*>(this), verificationProvider);

                //
                // DEX
                //

                auto dexBoard = make_shared<DexBoard>(*broadcastRouter, this->getAsync(), *m_walletDB);
                auto dexWDBSubscriber = make_unique<WalletDbSubscriber>(static_cast<IWalletDbObserver*>(dexBoard.get()), m_walletDB);

                using DexBoardSubscriber = ScopedSubscriber<DexBoard::IObserver, DexBoard>;
                auto dexBoardSubscriber = make_unique<DexBoardSubscriber>(static_cast<DexBoard::IObserver*>(this), dexBoard);

                _dex = dexBoard;


                //
                // IPFS
                //
                #ifdef BEAM_IPFS_SUPPORT
                struct IPFSHandler : public IPFSService::Handler {
                    explicit IPFSHandler(WalletClient *wc)
                            : _wc(wc) {
                    }

                    void AnyThread_pushToClient(std::function<void()> &&action) override {
                        _wc->postFunctionToClientContext(std::move(action));
                    }

                    void AnyThread_onStatus(const std::string& error, uint32_t peercnt) override {
                        _wc->getAsync()->makeIWTCall([this, error, peercnt]() -> boost::any {
                            if (error.empty()) {
                                LOG_INFO() << "IPFS Status: peercnt " << peercnt;
                            } else {
                                LOG_INFO() << "IPFS Status: peercnt " << peercnt << ", error: " << error;
                            }
                            _wc->m_ipfsError = error;
                            _wc->m_ipfsPeerCnt = static_cast<unsigned int>(peercnt);
                            _wc->getIPFSStatus();
                            return boost::none;
                        }, [](const boost::any&) {
                            // client thread
                        });
                    }

                private:
                    WalletClient *_wc;
                };

                std::shared_ptr<IPFSHandler> ipfsHandler;
                std::shared_ptr<IPFSService> ipfsService;

                LOG_INFO() << "IPFS Service is enabled.";
                ipfsHandler = std::make_shared<IPFSHandler>(this);
                ipfsService = IPFSService::AnyThread_create(ipfsHandler);
                m_ipfs = ipfsService;
                #else
                LOG_INFO () << "IPFS Service is disabled.";
                #endif

                //
                // Shaders
                //
                auto clientShaders = IShadersManager::CreateInstance(wallet, m_walletDB, nodeNetwork, "", "", 0);
                _clientShaders = clientShaders;

                nodeNetwork->tryToConnect();
                m_reactor->run_ex([&wallet, &nodeNetwork](){
                    wallet->CleanupNetwork();
                    nodeNetwork->Disconnect();
                });

                #ifdef BEAM_IPFS_SUPPORT
                // IPFS service might be not started, so need to check
                if (ipfsService) 
                {
                    if (ipfsService->AnyThread_running()) {
                        ipfsService->ServiceThread_stop();
                    }

                    assert(ipfsService.use_count() == 1);
                    ipfsService.reset();
                }
                #endif

                wallet->CleanupNetwork();
                nodeNetwork->Disconnect();

                assert(clientShaders.use_count() == 1);
                clientShaders.reset();

                assert(walletNetwork.use_count() == 1);
                walletNetwork.reset();

                nodeNetworkSubscriber.reset();
                broadcastRouter.reset();
                assert(nodeNetwork.use_count() == 1);
                nodeNetwork.reset();

                m_balanceDelayed->cancel();
                m_ainfoDelayed->cancel();
            }
            catch (const runtime_error& ex)
            {
                LOG_ERROR() << ex.what();
                FailedToStartWallet();
            }
            catch (const std::exception& ex)
            {
                LOG_UNHANDLED_EXCEPTION() << "what = " << ex.what();
            }
            /*catch (...) {
                LOG_UNHANDLED_EXCEPTION();
            }*/
            onStopped();
        });
    }

    IWalletModelAsync::Ptr WalletClient::getAsync()
    {
        return m_async;
    }

    Wallet::Ptr WalletClient::getWallet()
    {
        auto sp = m_wallet.lock();
        return sp;
    }

    NodeNetwork::Ptr WalletClient::getNodeNetwork()
    {
        auto sp = m_nodeNetwork.lock();
        return sp;
    }

    #ifdef BEAM_IPFS_SUPPORT
    IPFSService::Ptr WalletClient::getIPFS()
    {
        auto sp = m_ipfs.lock();
        if(!sp || !sp->AnyThread_running())
        {
            assert(false);
            throw std::runtime_error("IPFS Service is not running");
        }
        return sp;
    }

    IPFSService::Ptr WalletClient::IWThread_startIPFSNode()
    {
        if (!m_ipfsConfig)
        {
            assert(false);
            throw std::runtime_error("IPFS config is not provided");
        }

        auto sp = m_ipfs.lock();
        if (!sp)
        {
            assert(false);
            throw std::runtime_error("IPFS service is not created");
        }

        if (!sp->AnyThread_running())
        {
            // throws
            sp->ServiceThread_start(*m_ipfsConfig);
        }

        return sp;
    }

    void WalletClient::startIPFSNode()
    {
        try
        {
            IWThread_startIPFSNode();
        }
        catch(std::runtime_error& err)
        {
            auto errmsg = std::string("Failed to start IPFS service. ") + err.what();
            LOG_ERROR() << errmsg;
            m_ipfsError = errmsg;
            m_ipfsPeerCnt = 0;
            getIPFSStatus();
        }
    }

    void WalletClient::IWThread_setIPFSConfig(asio_ipfs::config&& cfg)
    {
        //
        // if service is already running restart with new settings
        // otherwise we just store settings for future use
        //
        m_ipfsConfig = std::move(cfg);

        auto sp = m_ipfs.lock();
        if (!sp)
        {
            assert(false);
            throw std::runtime_error("IPFS service is not created");
        }

        if (sp->AnyThread_running())
        {
            sp->ServiceThread_stop();
            sp->ServiceThread_start(*m_ipfsConfig);
        }
    }

    void WalletClient::setIPFSConfig(asio_ipfs::config&& cfg)
    {
        try
        {
            IWThread_setIPFSConfig(std::move(cfg));
        }
        catch(std::runtime_error& err)
        {
            auto errmsg = std::string("Failed to start IPFS service. ") + err.what();
            LOG_ERROR() << errmsg;
            m_ipfsError = errmsg;
            m_ipfsPeerCnt = 0;
            getIPFSStatus();
        }
    }

    void WalletClient::IWThread_stopIPFSNode()
    {
        auto sp = m_ipfs.lock();
        if (!sp)
        {
            assert(false);
            throw std::runtime_error("IPFS service is not created");
        }

        if (sp->AnyThread_running()) 
        {
            sp->ServiceThread_stop();
        }
    }

    void WalletClient::stopIPFSNode()
    {
        try
        {
            IWThread_stopIPFSNode();
        }
        catch(std::runtime_error& err)
        {
            auto errmsg = std::string("Failed to start IPFS service. ") + err.what();
            LOG_ERROR() << errmsg;
            m_ipfsError = errmsg;
            m_ipfsPeerCnt = 0;
            getIPFSStatus();
        }
    }
    #endif //BEAM_IPFS_SUPPORT

    IShadersManager::Ptr WalletClient::IWThread_createAppShaders(const std::string& appid, const std::string& appname, uint32_t privilegeLvl)
    {
        auto wallet = m_wallet.lock();
        auto network = m_nodeNetwork.lock();
        assert(wallet && network);
        return IShadersManager::CreateInstance(wallet, m_walletDB, network, appid, appname, privilegeLvl);
    }

    std::string WalletClient::getNodeAddress() const
    {
        if (auto s = m_nodeNetwork.lock())
        {
            return s->getNodeAddress();
        }
        else
        {
            return m_initialNodeAddrStr;
        }
    }

    std::string WalletClient::exportOwnerKey(const beam::SecString& pass) const
    {
        // TODO: remove this, it is not thread safe
        Key::IPKdf::Ptr pOwner = m_walletDB->get_OwnerKdf();

        KeyString ks;
        ks.SetPassword(Blob(pass.data(), static_cast<uint32_t>(pass.size())));
        ks.m_sMeta = std::to_string(0);

        ks.ExportP(*pOwner);

        return ks.m_sRes;
    }

    bool WalletClient::isRunning() const
    {
        return m_thread && m_thread->joinable();
    }

    bool WalletClient::isFork1() const
    {
        return m_currentHeight >= getRules().pForks[1].m_Height;
    }

    size_t WalletClient::getUnsafeActiveTransactionsCount() const
    {
        return m_unsafeActiveTxCount;
    }

    size_t WalletClient::getUnreadNotificationsCount() const
    {
        return m_unreadNotificationsCount;
    }

    bool WalletClient::isConnectionTrusted() const
    {
        return m_isConnectionTrusted;
    }

    bool WalletClient::isSynced() const
    {
        return m_isSynced;
    }

    beam::TxoID WalletClient::getTotalShieldedCount() const
    {
        return m_status.shieldedTotalCount;
    }

    uint8_t WalletClient::getMPLockTimeLimit() const
    {
        return m_mpLockTimeLimit;
    }

    uint32_t WalletClient::getMarurityProgress(const ShieldedCoin& coin) const
    {
        ShieldedCoin::UnlinkStatus us(coin, getTotalShieldedCount());
        const auto* packedMessage = ShieldedTxo::User::ToPackedMessage(coin.m_CoinID.m_User);
        uint32_t mpAnonymitySet = packedMessage->m_MaxPrivacyMinAnonymitySet;
        return mpAnonymitySet ? us.m_Progress * mpAnonymitySet / beam::MaxPrivacyAnonimitySetFractionsCount : us.m_Progress;
    }

    uint16_t WalletClient::getMaturityHoursLeft(const ShieldedCoin& coin) const
    {
        auto& timeLimit = m_mpLockTimeLimit;

        uint16_t hoursLeftByBlocksU = 0;
        if (timeLimit)
        {
            auto& stateID = m_status.stateID;
            auto hoursLeftByBlocks = (coin.m_confirmHeight + static_cast<uint32_t>(timeLimit) * 60 - stateID.m_Height) / 60.;
            hoursLeftByBlocksU = static_cast<uint16_t>(hoursLeftByBlocks > 1 ? floor(hoursLeftByBlocks) : ceil(hoursLeftByBlocks));
        }

        if (m_shieldedPer24h)
        {
            auto outputsAddedAfterMyCoin = getTotalShieldedCount() - coin.m_TxoID;
            const auto* packedMessage = ShieldedTxo::User::ToPackedMessage(coin.m_CoinID.m_User);
            
            auto mpAnonymitySet = packedMessage->m_MaxPrivacyMinAnonymitySet;
            auto maxWindowBacklog = mpAnonymitySet ? getRules().Shielded.MaxWindowBacklog * mpAnonymitySet / 64 : getRules().Shielded.MaxWindowBacklog;
            auto outputsLeftForMP = maxWindowBacklog - outputsAddedAfterMyCoin;
            auto hoursLeft = outputsLeftForMP / static_cast<double>(m_shieldedPer24h) * 24;
            uint16_t hoursLeftU = static_cast<uint16_t>(hoursLeft > 1 ? floor(hoursLeft) : ceil(hoursLeft));
            if (timeLimit)
            {
                hoursLeftU = std::min(hoursLeftU, hoursLeftByBlocksU);
            }
            return hoursLeftU;
        }

        return timeLimit ? hoursLeftByBlocksU : std::numeric_limits<uint16_t>::max();
    }

    const Rules& WalletClient::getRules() const
    {
        return m_rules;
    }

    std::set<beam::Asset::ID> WalletClient::getAssetsFull() const
    {
        std::set<beam::Asset::ID> assets;
        // assets.insert(Asset::s_BeamID);

        // for (const auto& status : m_status.all)
        //     assets.insert(status.first);

        return assets;
    }

    std::set<beam::Asset::ID> WalletClient::getAssetsNZ() const
    {
        std::set<beam::Asset::ID> assets;

        // always have BEAM, even if zero
        assets.insert(Asset::s_BeamID);

        for (const auto& status : m_status.all)
        {
            const auto& totals = status.second;
            if (totals.available != Zero || totals.maturing != Zero || totals.maturingMP != Zero ||
                totals.receiving != Zero || totals.receivingChange != Zero || totals.receivingIncoming != Zero ||
                totals.sending != Zero || totals.shielded != Zero)
            {
                assets.insert(status.first);
            }
        }

        return assets;
    }

    beam::AmountBig::Type WalletClient::getAvailable(beam::Asset::ID id) const
    {
        const auto& status = m_status.GetStatus(id);

        auto result = status.available;
        result += status.shielded;

        return result;
    }


    beam::AmountBig::Type WalletClient::getAvailableRegular(beam::Asset::ID id) const
    {
        const auto& status = m_status.GetStatus(id);
        return status.available;
    }

    beam::AmountBig::Type WalletClient::getAvailableShielded(beam::Asset::ID id) const
    {
        const auto& status = m_status.GetStatus(id);
        return status.shielded;
    }

    beam::AmountBig::Type WalletClient::getReceiving(beam::Asset::ID id) const
    {
        const auto& status = m_status.GetStatus(id);
        return status.receiving;
    }

    beam::AmountBig::Type WalletClient::getReceivingIncoming(beam::Asset::ID id) const
    {
        const auto& status = m_status.GetStatus(id);
        return status.receivingIncoming;
    }

    beam::AmountBig::Type WalletClient::getMatutingMP(beam::Asset::ID id) const
    {
        const auto& status = m_status.GetStatus(id);
        return status.maturingMP;
    }

    bool WalletClient::hasShielded(beam::Asset::ID id) const
    {
        const auto& status = m_status.GetStatus(id);
        return status.shielded != Zero;
    }

    beam::AmountBig::Type WalletClient::getReceivingChange(beam::Asset::ID id) const
    {
        const auto& status = m_status.GetStatus(id);
        return status.receivingChange;
    }

    beam::AmountBig::Type WalletClient::getSending(beam::Asset::ID id) const
    {
        const auto& status = m_status.GetStatus(id);
        return status.sending;
    }

    beam::AmountBig::Type WalletClient::getMaturing(beam::Asset::ID id) const
    {
        const auto& status = m_status.GetStatus(id);
        return status.maturing;
    }

    beam::Height WalletClient::getCurrentHeight() const
    {
        return m_status.stateID.m_Height;
    }

    beam::Timestamp WalletClient::getCurrentHeightTimestamp() const
    {
        return m_status.update.lastTime;
    }

    beam::Timestamp WalletClient::getAverageBlockTime() const
    {
        return m_averageBlockTime;
    }

    beam::Timestamp WalletClient::getLastBlockTime() const
    {
        return m_lastBlockTime;
    }

    beam::Block::SystemState::ID WalletClient::getCurrentStateID() const
    {
        return m_status.stateID;
    }

    /////////////////////////////////////////////
    /// IWalletClientAsync implementation, these method are called in background thread and could safelly access wallet DB

    void WalletClient::onCoinsChanged(ChangeAction action, const std::vector<Coin>& items)
    {
        m_CoinChangesCollector.CollectItems(action, items);
        scheduleBalance();
    }

    void WalletClient::scheduleBalance()
    {
        m_balanceDelayed->start(0, false, [this] () {
            updateStatus();
        });
    }

    void WalletClient::onTransactionChanged(ChangeAction action, const std::vector<TxDescription>& items)
    {
        if (action == ChangeAction::Added)
        {
            for (const auto& tx : items)
            {
                if (!memis0(&tx.m_txId.front(), tx.m_txId.size())) // not special transaction
                {
                    assert(!m_exchangeRateProvider.expired());
                    if (auto p = m_exchangeRateProvider.lock())
                    {
                        m_walletDB->setTxParameter(tx.m_txId,
                            kDefaultSubTxID,
                            TxParameterID::ExchangeRates,
                            toByteBuffer(p->getRates()),
                            false);
                    }
                }
            }
        }
        m_TransactionChangesCollector.CollectItems(action, items);
        updateClientTxState();
    }

    void WalletClient::onSystemStateChanged(const Block::SystemState::ID& stateID)
    {
        updateStatus();
    }

    void WalletClient::onAddressChanged(ChangeAction action, const std::vector<WalletAddress>& items)
    {
        m_AddressChangesCollector.CollectItems(action, items);
    }

    void WalletClient::onShieldedCoinsChanged(ChangeAction action, const std::vector<ShieldedCoin>& coins)
    {
        // add virtual transaction for receiver
#ifdef BEAM_LELANTUS_SUPPORT
        m_ShieldedCoinChangesCollector.CollectItems(action, coins);
        scheduleBalance();
#endif // BEAM_LELANTUS_SUPPORT
    }

    void WalletClient::onSyncProgress(int done, int total)
    {
        auto w = m_wallet.lock();
        if (w)
        {
            postFunctionToClientContext([this, isSynced = ((done == total) && w->IsWalletInSync())]()
            {
                m_isSynced = isSynced;
            });
        }
        
        onSyncProgressUpdated(done, total);
    }

    void WalletClient::onOwnedNode(const PeerID& id, bool connected)
    {
        updateConnectionTrust(connected);
        onNodeConnectionChanged(isConnected());
    }

    void WalletClient::sendMoney(const WalletID& receiver, const std::string& comment, Amount amount, Amount fee)
    {
        try
        {
            assert(!m_wallet.expired());
            auto s = m_wallet.lock();
            if (s)
            {
                WalletAddress senderAddress;
                m_walletDB->createAddress(senderAddress);
                saveAddress(senderAddress); // should update the wallet_network
                ByteBuffer message(comment.begin(), comment.end());

                TxParameters txParameters = CreateSimpleTransactionParameters()
                    .SetParameter(TxParameterID::MyID, senderAddress.m_walletID)
                    .SetParameter(TxParameterID::PeerID, receiver)
                    .SetParameter(TxParameterID::Amount, amount)
                    .SetParameter(TxParameterID::Fee, fee)
                    .SetParameter(TxParameterID::Message, message);

                s->StartTransaction(txParameters);
            }

            onSendMoneyVerified();
        }
        catch (const CannotGenerateSecretException&)
        {
            onNewAddressFailed();
            return;
        }
        catch (const ReceiverAddressExpiredException&)
        {
            onCantSendToExpired();
            return;
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...) {
            LOG_UNHANDLED_EXCEPTION();
        }
    }

    void WalletClient::sendMoney(const WalletID& sender, const WalletID& receiver, const std::string& comment, Amount amount, Amount fee)
    {
        try
        {
            assert(!m_wallet.expired());
            auto s = m_wallet.lock();
            if (s)
            {
                ByteBuffer message(comment.begin(), comment.end());
                TxParameters txParameters = CreateSimpleTransactionParameters()
                    .SetParameter(TxParameterID::MyID, sender)
                    .SetParameter(TxParameterID::PeerID, receiver)
                    .SetParameter(TxParameterID::Amount, amount)
                    .SetParameter(TxParameterID::Fee, fee)
                    .SetParameter(TxParameterID::Message, message);
                
                s->StartTransaction(txParameters);
            }

            onSendMoneyVerified();
        }
        catch (const CannotGenerateSecretException&)
        {
            onNewAddressFailed();
            return;
        }
        catch (const ReceiverAddressExpiredException&)
        {
            onCantSendToExpired();
            return;
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...) {
            LOG_UNHANDLED_EXCEPTION();
        }
    }

    void WalletClient::startTransaction(TxParameters&& parameters)
    {
        try
        {
            assert(!m_wallet.expired());
            auto s = m_wallet.lock();
            if (s)
            {
                auto myID = parameters.GetParameter<WalletID>(TxParameterID::MyID);
                if (!myID)
                {
                    WalletAddress senderAddress;
                    m_walletDB->createAddress(senderAddress);
                    saveAddress(senderAddress); // should update the wallet_network
                    parameters.SetParameter(TxParameterID::MyID, senderAddress.m_walletID);
                }

                s->StartTransaction(parameters);
            }

            onSendMoneyVerified();
        }
        catch (const CannotGenerateSecretException&)
        {
            onNewAddressFailed();
            return;
        }
        catch (const ReceiverAddressExpiredException&)
        {
            onCantSendToExpired();
            return;
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...) {
            LOG_UNHANDLED_EXCEPTION();
        }
    }

    void WalletClient::syncWithNode()
    {
        assert(!m_nodeNetwork.expired());
        if (auto s = m_nodeNetwork.lock())
        {
            s->Connect();
    }
    }

    void WalletClient::calcChange(Amount amount, Amount fee, Asset::ID assetId)
    {
        CoinsSelectionInfo csi;
        csi.m_requestedSum = amount;
        csi.m_assetID = assetId;
        csi.m_explicitFee = fee;
        csi.Calculate(m_currentHeight, m_walletDB, false);
        onChangeCalculated(csi.m_changeAsset, csi.m_changeBeam, assetId);
    }

    void WalletClient::selectCoins(Amount requested, Amount beforehandMinFee, Asset::ID assetId, bool isShielded /* = false */)
    {
        CoinsSelectionInfo csi;
        csi.m_requestedSum = requested;
        csi.m_assetID = assetId;
        csi.m_explicitFee = beforehandMinFee;
        csi.Calculate(m_currentHeight, m_walletDB, isShielded);
        onCoinsSelected(csi);
    }

     void WalletClient::selectCoins(Amount amount, Amount beforehandMinFee, Asset::ID assetId, bool isShielded, AsyncCallback<const CoinsSelectionInfo&>&& callback)
     {
        CoinsSelectionInfo csi;
        csi.m_requestedSum = amount;
        csi.m_assetID = assetId;
        csi.m_explicitFee = beforehandMinFee;
        csi.Calculate(m_currentHeight, m_walletDB, isShielded);
        postFunctionToClientContext([csi, cback = std::move(callback)]()
        {
            cback(csi);
        });
     }

    void WalletClient::getWalletStatus()
    {
        onStatus(getStatus());
    }

    void WalletClient::getTransactions()
    {
        onTransactionChanged(ChangeAction::Reset, m_walletDB->getTxHistory(wallet::TxType::ALL));
    }

    void WalletClient::getTransactions(AsyncCallback<const std::vector<TxDescription>&>&& callback)
    {
        postFunctionToClientContext([res = m_walletDB->getTxHistory(wallet::TxType::ALL), cb = std::move(callback)]()
        {
            cb(res);
        });
    }

    void WalletClient::getTransactionsSmoothly()
    {
        //auto txCount = m_walletDB->getTxCount(wallet::TxType::ALL);
        //if (txCount > kOneTimeLoadTxCount)
        //{
        //    onTransactionChanged(ChangeAction::Reset, vector<wallet::TxDescription>());

        //    auto iterationsCount = txCount / kOneTimeLoadTxCount + (txCount % kOneTimeLoadTxCount ? 1 : 0);
        //    for(int i = 0; i < iterationsCount; ++i) 
        //        onTransactionChanged(
        //            ChangeAction::Added, 
        //            m_walletDB->getTxHistory(wallet::TxType::ALL,
        //                                     i * kOneTimeLoadTxCount,
        //                                     i * kOneTimeLoadTxCount + kOneTimeLoadTxCount));
        //} 
        //else
        //{
            getTransactions();
        //}
    }

    void WalletClient::getAllUtxosStatus()
    {
        onCoinsChanged(ChangeAction::Reset, m_walletDB->getAllNormalCoins());
        onShieldedCoinsChanged(ChangeAction::Reset, m_walletDB->getAllShieldedCoins());
    }

    void WalletClient::getAddress(const WalletID& addr)
    {
        try
        {
            const auto address = m_walletDB->getAddress(addr);
            onGetAddress(addr, address, m_walletDB->getVoucherCount(addr));
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...) {
            LOG_UNHANDLED_EXCEPTION();
        }
    }

    void WalletClient::getAddresses(bool own)
    {
        onAddresses(own, m_walletDB->getAddresses(own));
    }

    void WalletClient::getDexOrders()
    {
        if (auto dex = _dex.lock())
        {
            onDexOrdersChanged(ChangeAction::Reset, dex->getOrders());
        }
    }

    void WalletClient::publishDexOrder(const DexOrder& order)
    {
        if (auto dex = _dex.lock())
        {
            dex->publishOrder(order);
            return;
        }

        assert(false);
        LOG_WARNING() << "WalletClient::publishDexOrder but DEX is not available";
    }

    void WalletClient::acceptDexOrder(const DexOrderID& orderId)
    {
        if (auto dex = _dex.lock())
        {
            dex->acceptOrder(orderId);
            return;
        }

        assert(false);
        LOG_WARNING() << "WalletClient::acceptDexOrder but DEX is not available";
    }

    void WalletClient::getAssetSwapOrders()
    {

    }

    void WalletClient::publishAssetSwapOrder(const AssetSwapOrder& order)
    {
        if (auto dex = _dex.lock())
        {
            dex->publishOrder(order);
            return;
        }
    }

    void WalletClient::acceptAssetSwapOrder(const DexOrderID&)
    {

    }

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
    void WalletClient::getSwapOffers()
    {
        if (auto p = m_offersBulletinBoard.lock())
        {
            onSwapOffersChanged(ChangeAction::Reset, p->getOffersList());
        }
    }

    void WalletClient::publishSwapOffer(const SwapOffer& offer)
    {
        if (auto p = m_offersBulletinBoard.lock())
        {
            try
            {
                p->publishOffer(offer);
            }
            catch (const std::runtime_error& e)
            {
                LOG_ERROR() << offer.m_txId << e.what();
            }
        }
    }

    namespace {
        const char* SWAP_PARAMS_NAME = "LastSwapParams";
    }

    void WalletClient::loadSwapParams()
    {
        ByteBuffer params;
        m_walletDB->getBlob(SWAP_PARAMS_NAME, params);
        onSwapParamsLoaded(params);
    }

    void WalletClient::storeSwapParams(const ByteBuffer& params)
    {
        m_walletDB->setVarRaw(SWAP_PARAMS_NAME, params.data(), params.size());
    }
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

    void WalletClient::loadAssetSwapParams()
    {
        ByteBuffer params;
        m_walletDB->getBlob(ASSET_SWAP_PARAMS_NAME, params);
        onAssetSwapParamsLoaded(params);
    }

    void WalletClient::storeAssetSwapParams(const ByteBuffer& params)
    {
        m_walletDB->setVarRaw(ASSET_SWAP_PARAMS_NAME, params.data(), params.size());
    }

    void WalletClient::cancelTx(const TxID& id)
    {
        auto w = m_wallet.lock();
        if (w)
        {
            w->CancelTransaction(id);
        }
    }

    void WalletClient::deleteTx(const TxID& id)
    {
        auto w = m_wallet.lock();
        if (w)
        {
            w->DeleteTransaction(id);
        }
    }

    void WalletClient::getCoinsByTx(const TxID& id)
    {
        onCoinsByTx(m_walletDB->getCoinsByTx(id));
    }

    void WalletClient::saveAddress(const WalletAddress& address)
    {
        m_walletDB->saveAddress(address);
    }

    void WalletClient::generateNewAddress()
    {
        try
        {
            WalletAddress address;
            m_walletDB->createAddress(address);
            onGeneratedNewAddress(address);
        }
        catch (const CannotGenerateSecretException&)
        {
            onNewAddressFailed();
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...) {
            LOG_UNHANDLED_EXCEPTION();
        }
    }

    void WalletClient::generateNewAddress(AsyncCallback<const WalletAddress&>&& callback)
    {
        try
        {
            WalletAddress address;
            m_walletDB->createAddress(address);

            postFunctionToClientContext([address, cb = std::move(callback)]()
            {
                cb(address);
            });
        }
        catch (const CannotGenerateSecretException&)
        {
            onNewAddressFailed();
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...) {
            LOG_UNHANDLED_EXCEPTION();
        }
    }

    void WalletClient::deleteAddress(const WalletID& addr)
    {
        try
        {
            m_walletDB->deleteAddress(addr);
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...) {
            LOG_UNHANDLED_EXCEPTION();
        }
    }

    void WalletClient::deleteAddressByToken(const std::string& token)
    {
        try
        {
            m_walletDB->deleteAddressByToken(token);
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...) {
            LOG_UNHANDLED_EXCEPTION();
        }
    }

    void WalletClient::updateAddress(const WalletID& wid, const std::string& name, WalletAddress::ExpirationStatus expirationStatus)
    {
        try
        {
            auto addr = m_walletDB->getAddress(wid);

            if (addr)
            {
                if (addr->isOwn())
                {
                    addr->setExpirationStatus(expirationStatus);
                }
                addr->setLabel(name);
                m_walletDB->saveAddress(*addr);
            }
            else
            {
                LOG_ERROR() << "Address " << to_string(wid) << " is absent.";
            }
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
    }

    void WalletClient::updateAddress(const WalletID& wid, const std::string& name, beam::Timestamp expirationTime)
    {
        try
        {
            auto addr = m_walletDB->getAddress(wid);

            if (addr)
            {
                if (addr->isOwn())
                {
                    addr->setExpirationTime(expirationTime);
                }
                addr->setLabel(name);
                m_walletDB->saveAddress(*addr);
            }
            else
            {
                LOG_ERROR() << "Address " << to_string(wid) << " is absent.";
            }
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
    }

    void WalletClient::activateAddress(const WalletID& wid)
    {
        try
        {
            auto addr = m_walletDB->getAddress(wid);
            if (addr)
            {
                if (addr->isOwn())
                {
                    addr->setExpirationStatus(WalletAddress::ExpirationStatus::Auto);
                }
                m_walletDB->saveAddress(*addr);
            }
            else
            {
                LOG_ERROR() << "Address " << to_string(wid) << " is absent.";
            }
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...)
        {
            LOG_UNHANDLED_EXCEPTION();
        }
    }


    void WalletClient::getAddressByToken(const std::string& token, AsyncCallback<const boost::optional<WalletAddress>&, size_t>&& callback)
    {
        auto addr = m_walletDB->getAddressByToken(token);

        size_t vouchersCount = 0;
        if (addr && addr->m_walletID != Zero)
        {
            vouchersCount = m_walletDB->getVoucherCount(addr->m_walletID);
        }

        postFunctionToClientContext([addr, vouchersCount, cb = std::move(callback)]() {
            cb(addr, vouchersCount);
        });
    }

    void WalletClient::getAddress(const WalletID& wid, AsyncCallback<const boost::optional<WalletAddress>&, size_t>&& callback)
    {
        try
        {
            auto addr = m_walletDB->getAddress(wid);
            size_t vouchersCount = m_walletDB->getVoucherCount(wid);

            postFunctionToClientContext([addr, vouchersCount, cb = std::move(callback)]()
            {
                cb(addr, vouchersCount);
            });
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...)
        {
            LOG_UNHANDLED_EXCEPTION();
        }
    }

    void WalletClient::saveVouchers(const ShieldedVoucherList& vouchers, const WalletID& walletID)
    {
        try
        {
            storage::SaveVouchers(*m_walletDB, vouchers, walletID);
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...) 
        {
            LOG_UNHANDLED_EXCEPTION();
        }
    }

    void WalletClient::setNodeAddress(const std::string& addr)
    {
        if (auto s = m_nodeNetwork.lock())
        {
            if (!(s->setNodeAddress(addr)))
            {
                LOG_ERROR() << "Unable to resolve node address: " << addr;
                onWalletError(ErrorType::HostResolvedError);
            }
        }
        else
        {
            io::Address address;
            if (address.resolve(addr.c_str()))
            {
                m_initialNodeAddrStr = addr;
            }
            else
            {
                LOG_ERROR() << "Unable to resolve node address: " << addr;
                onWalletError(ErrorType::HostResolvedError);
            }
        }
    }

    void WalletClient::changeWalletPassword(const SecString& pass)
    {
        m_walletDB->changePassword(pass);
    }

    void WalletClient::getNetworkStatus()
    {
        if (m_walletError.is_initialized() && !isConnected())
        {
            onWalletError(*m_walletError);
            return;
        }

        onNodeConnectionChanged(isConnected());
    }

    #ifdef BEAM_IPFS_SUPPORT
    void WalletClient::getIPFSStatus()
    {
        auto sp = m_ipfs.lock();
        if (!sp) {
            onIPFSStatus(false, std::string(), 0);
            return;
        }

        auto running = sp->AnyThread_running();
        if (running && m_ipfsPeerCnt == 0 && m_ipfsError.empty()) {
            onIPFSStatus(running, "IPFS node is not connected to peers", m_ipfsPeerCnt);
            return;
        }

        onIPFSStatus(running, m_ipfsError, m_ipfsPeerCnt);
    }
    #endif

    void WalletClient::rescan()
    {
        try
        {
            assert(!m_wallet.expired());
            auto s = m_wallet.lock();
            if (s)
            {
                s->Rescan();
            }
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...)
        {
            LOG_UNHANDLED_EXCEPTION();
        }
    }

    void WalletClient::exportPaymentProof(const TxID& id)
    {
        onPaymentProofExported(id, storage::ExportPaymentProof(*m_walletDB, id));
    }

    void WalletClient::checkNetworkAddress(const std::string& addr)
    {
        io::Address nodeAddr;
        onAddressChecked(addr, nodeAddr.resolve(addr.c_str()));
    }

    void WalletClient::importRecovery(const std::string& path)
    {
        try
        {
            if (auto w = getWallet())
            {
                if (!m_walletDB->ImportRecovery(path, *w, *this))
                {
                    onWalletError(ErrorType::ImportRecoveryError);
                }
            }
            return;
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...) 
        {
            LOG_UNHANDLED_EXCEPTION();
        }
        onWalletError(ErrorType::ImportRecoveryError);
    }

    void WalletClient::importDataFromJson(const std::string& data)
    {
        auto isOk = storage::ImportDataFromJson(*m_walletDB, data.data(), data.size());

        onImportDataFromJson(isOk);
    }

    void WalletClient::exportDataToJson()
    {
        auto data = storage::ExportDataToJson(*m_walletDB);

        onExportDataToJson(data);
    }

    void WalletClient::exportTxHistoryToCsv()
    {
        auto data = ExportTxHistoryToCsv(*m_walletDB);
        onExportTxHistoryToCsv(data);
    }

    void WalletClient::switchOnOffExchangeRates(bool isActive)
    {
        assert(!m_exchangeRateProvider.expired());
        if (auto s = m_exchangeRateProvider.lock())
        {
            s->setOnOff(isActive);
        }
    }

    void WalletClient::switchOnOffNotifications(Notification::Type type, bool isActive)
    {
        m_notificationCenter->switchOnOffNotifications(type, isActive);
    }
    
    void WalletClient::getNotifications()
    {
        onNotificationsChanged(ChangeAction::Reset, m_notificationCenter->getNotifications());
    }

    void WalletClient::markNotificationAsRead(const ECC::uintBig& id)
    {
        m_notificationCenter->markNotificationAsRead(id);
    }

    void WalletClient::deleteNotification(const ECC::uintBig& id)
    {
        m_notificationCenter->deleteNotification(id);
    }

    void WalletClient::getExchangeRates()
    {
        assert(!m_exchangeRateProvider.expired());
        if (auto s = m_exchangeRateProvider.lock())
        {
            onExchangeRates(s->getRates());
        }
        else
        {
            onExchangeRates({});
        }
    }

    void WalletClient::getVerificationInfo()
    {
        assert(!m_verificationProvider.expired());
        if (auto s = m_verificationProvider.lock())
        {
            const auto info = s->getInfo();
            onVerificationInfo(info);
        }
        else
        {
            onVerificationInfo({});
        }
    }

    void WalletClient::getPublicAddress()
    {
        onPublicAddress(GeneratePublicToken(*m_walletDB, std::string()));
    }

    void WalletClient::generateVouchers(uint64_t ownID, size_t count, AsyncCallback<const ShieldedVoucherList&>&& callback)
    {
        auto vouchers = GenerateVoucherList(m_walletDB->get_KeyKeeper(), ownID, count);
        postFunctionToClientContext([res = std::move(vouchers), cb = std::move(callback)]() 
        {
            cb(res);
        });
    }

    void WalletClient::setMaxPrivacyLockTimeLimitHours(uint8_t limit)
    {
        m_walletDB->set_MaxPrivacyLockTimeLimitHours(limit);
        updateStatus();
    }

    void WalletClient::getMaxPrivacyLockTimeLimitHours(AsyncCallback<uint8_t>&& callback)
    {
        auto limit = m_walletDB->get_MaxPrivacyLockTimeLimitHours();
        postFunctionToClientContext([res = std::move(limit), cb = std::move(callback)]() 
        {
            cb(res);
        });
    }

    void WalletClient::setCoinConfirmationsOffset(uint32_t val)
    {
        m_walletDB->setCoinConfirmationsOffset(val);
    }

    void WalletClient::removeRawSeedPhrase()
    {
        m_walletDB->removeVarRaw(SEED_PARAM_NAME);
    }

    void WalletClient::readRawSeedPhrase(AsyncCallback<const std::string&>&& callback)
    {
        ByteBuffer b;
        if (m_walletDB->getBlob(SEED_PARAM_NAME, b))
        {
            std::string phrase((char*)b.data());
            postFunctionToClientContext([res = std::move(phrase), cb = std::move(callback)]() 
            {
                cb(res);
            });
        }
    }

    namespace
    {
        constexpr auto getAppsUrl()
        {
#ifdef BEAM_BEAMX
            return "";
#elif defined(BEAM_TESTNET)
            return "https://apps-testnet.beam.mw/appslist.json";
#elif defined(BEAM_MAINNET)
            return "https://apps.beam.mw/appslist.json";
#elif defined(BEAM_DAPPNET)
            return "https://apps-dappnet.beam.mw/app/appslist.json";
#else
            return "http://3.19.141.112/app/appslist.json";
#endif
        }
    }

    void WalletClient::getAppsList(AppsListCallback&& callback)
    {
        io::Address address;

        constexpr auto url = getAppsUrl();

        static std::regex exrp("^(?:(http[s]?)://)?([^/]+)((/?.*/?)/(.*))$");
        std::smatch groups;
        std::string host;
        std::string path;
        std::string scheme;
        std::string myUrl(url);
        if (std::regex_match(myUrl, groups, exrp))
        {
            host.assign(groups[2].first, groups[2].second);
            path.assign(groups[3].first, groups[3].second);
            scheme.assign(groups[1].first, groups[1].second);
        }

        if (!address.resolve(host.c_str()))
        {
            LOG_ERROR() << "Unable to resolve address: " << host;

            postFunctionToClientContext([cb = std::move(callback)]()
            {
                cb(false, "");
            });
            return;
        }
        if (address.port() == 0)
        {
            if (scheme == "http")
                address.port(80);
            else if (scheme == "https")
                address.port(443);
        }

        std::vector<HeaderPair> headers;
        headers.push_back({ "Content-Type", "application/json" });
        headers.push_back({ "Host", host.c_str() });

        HttpClient::Request request;

        request.address(address)
            //.connectTimeoutMsec(2000)
            .pathAndQuery(path.c_str())
            .headers(&headers.front())
            .numHeaders(headers.size())
            .method("GET");

        request.callback([this, cb = std::move(callback)](uint64_t id, const HttpMsgReader::Message& msg) -> bool
        {
            bool isOk = false;
            std::string response;
            if (msg.what == HttpMsgReader::http_message)
            {
                size_t sz = 0;
                const void* body = msg.msg->get_body(sz);
                isOk = sz > 0 && body;
                if (isOk)
                {
                    response = std::string(static_cast<const char*>(body), sz);
                }
            }
            else if (msg.what == HttpMsgReader::connection_error)
            {
                LOG_ERROR() << "Failed to load application list: conection error(" << msg.connectionError << ")";
            }
            else if (msg.what == HttpMsgReader::message_corrupted)
            {
                LOG_ERROR() << "Failed to load application list: corrupted message";
            }
            else
            {
                LOG_ERROR() << "Failed to load application list reason: " << msg.what;
            }

            postFunctionToClientContext([isOk, res = std::move(response), callback = std::move(cb)]()
            {
                callback(isOk, res);
            });
            return false;
        });

        if (!m_httpClient)
        {
            m_httpClient = std::make_unique<HttpClient>(*m_reactor);
        }

        m_httpClient->send_request(request, scheme == "https");
    }

    void WalletClient::markAppNotificationAsRead(const TxID& id)
    {
        auto w = m_wallet.lock();
        if (w)
        {
            w->markAppNotificationAsRead(id);
        }
    }

    void WalletClient::getCoinConfirmationsOffset(AsyncCallback<uint32_t>&& callback)
    {
        auto confirmationOffset = m_walletDB->getCoinConfirmationsOffset();
        postFunctionToClientContext([res = std::move(confirmationOffset), cb = std::move(callback)]() 
        {
            cb(res);
        });
    }

    void WalletClient::enableBodyRequests(bool value)
    {
        auto s = m_wallet.lock();
        if (s)
        {
            s->EnableBodyRequests(value);
        }
    }

    bool WalletClient::OnProgress(uint64_t done, uint64_t total)
    {
        onImportRecoveryProgress(done, total);
        return true;
    }

    WalletStatus WalletClient::getStatus() const
    {
        WalletStatus status;
        storage::Totals allTotals(*m_walletDB, false);

        for(const auto& totalsPair: allTotals.GetAllTotals()) {
            const auto& info = totalsPair.second;
            WalletStatus::AssetStatus assetStatus;

            assetStatus.available         =  info.Avail;
            assetStatus.receivingIncoming =  info.ReceivingIncoming;
            assetStatus.receivingIncoming += info.IncomingShielded;
            assetStatus.receivingChange   =  info.ReceivingChange;
            assetStatus.receiving         =  info.Incoming;
            assetStatus.sending           =  info.Outgoing;
            assetStatus.sending           += info.OutgoingShielded;
            assetStatus.maturing          =  info.Maturing;
            assetStatus.maturingMP        =  info.MaturingShielded;
            assetStatus.shielded          =  info.AvailShielded;

            status.all[totalsPair.first] = assetStatus;
        }

        ZeroObject(status.stateID);
        m_walletDB->getSystemStateID(status.stateID);
        status.shieldedTotalCount = m_walletDB->get_ShieldedOuts();
        status.update.lastTime = m_walletDB->getLastUpdateTime();
        status.nzAssets = allTotals.GetAssetsNZ();

        return status;
    }

    void WalletClient::onNodeConnectionFailed(const proto::NodeConnection::DisconnectReason& reason)
    {
        // reason -> ErrorType
        if (proto::NodeConnection::DisconnectReason::ProcessingExc == reason.m_Type)
        {
            m_walletError = getWalletError(reason.m_ExceptionDetails.m_ExceptionType);
            onWalletError(*m_walletError);
            return;
        }

        if (proto::NodeConnection::DisconnectReason::Io == reason.m_Type)
        {
            m_walletError = getWalletError(reason.m_IoError);
            onWalletError(*m_walletError);
            return;
        }

        LOG_ERROR() << "Unprocessed error: " << reason;
    }

    void WalletClient::onNodeConnectedStatusChanged(bool isNodeConnected)
    {
        if (isNodeConnected)
        {
            ++m_connectedNodesCount;
            m_isSynced = false;
        }
        else if (m_connectedNodesCount)
        {
            --m_connectedNodesCount;
        }

        onNodeConnectionChanged(isConnected());
    }

    void WalletClient::updateStatus()
    {
        auto status = getStatus();
        updateMaxPrivacyStats(status);
        updateClientState(status);
        onStatus(status);
    }

    void WalletClient::updateClientState(const WalletStatus& status)
    {
        if (auto w = m_wallet.lock(); w)
        {
            postFunctionToClientContext([this, currentHeight = m_walletDB->getCurrentHeight()
                , count = w->GetUnsafeActiveTransactionsCount()
                , status
                , limit = m_walletDB->get_MaxPrivacyLockTimeLimitHours()]()
            {
                m_status = status;
                m_currentHeight = currentHeight;
                m_unsafeActiveTxCount = count;
                m_mpLockTimeLimit = limit;
            });

            auto currentHeight = w->get_TipHeight();

            struct Walker :public Block::SystemState::IHistory::IWalker
            {
                std::vector<Block::SystemState::Full> m_vStates;
                uint32_t m_Count;

                virtual bool OnState(const Block::SystemState::Full& s) override
                {
                    m_vStates.push_back(s);
                    return m_vStates.size() < m_Count;
                }
            } walker;

            walker.m_Count = 10;
            walker.m_vStates.reserve(10);
            Height historyHeight = currentHeight - 10;
            m_walletDB->get_History().Enum(walker, &historyHeight);

            if (walker.m_vStates.empty())
                return;

            auto oldest = walker.m_vStates[walker.m_vStates.size() - 1];
            Block::SystemState::Full curentState;
            m_walletDB->get_History().get_Tip(curentState);
            auto distance = currentHeight - oldest.m_Height;
            auto averageBlockTime = distance ? (curentState.m_TimeStamp - oldest.m_TimeStamp) / distance : 0; 
            auto lastBlockTime = curentState.m_TimeStamp;

            postFunctionToClientContext([this, averageBlockTime, lastBlockTime]()
            {
                m_averageBlockTime = averageBlockTime;
                m_lastBlockTime = lastBlockTime;
            });
        }
    }

    void WalletClient::updateMaxPrivacyStats(const WalletStatus& status)
    {
        if (!(status.stateID.m_Height % kShieldedPer24hFilterBlocksForUpdate))
        {
            updateMaxPrivacyStatsImpl(status);
        }
    }

    void WalletClient::updateMaxPrivacyStatsImpl(const WalletStatus& status)
    {
        m_shieldedCountHistoryPart.clear();

        if (status.stateID.m_Height > kShieldedPer24hFilterBlocksForUpdate * kShieldedCountHistoryWindowSize)
        {
            auto w = m_wallet.lock();
            if (!w)
            {
                return;
            }

            m_shieldedCountHistoryPart.reserve(kShieldedCountHistoryWindowSize);

            for (uint8_t i = 0; i < kShieldedCountHistoryWindowSize; ++i)
            {
                auto h = status.stateID.m_Height - (kShieldedPer24hFilterBlocksForUpdate * i);

                w->RequestShieldedOutputsAt(h, [this](Height h, TxoID count)
                {
                    m_shieldedCountHistoryPart.emplace_back(h, count);
                    if (m_shieldedCountHistoryPart.size() == kShieldedCountHistoryWindowSize)
                    {
                        for (uint8_t i = 0; i < kShieldedPer24hFilterSize; ++i)
                        {
                            if (m_shieldedCountHistoryPart[i].second)
                            {
                                double b = static_cast<double>(m_shieldedCountHistoryPart[i].second - m_shieldedCountHistoryPart[i + kShieldedPer24hFilterSize].second);
                                m_shieldedPer24hFilter->addSample(b);
                            }
                            else
                            {
                                m_shieldedPer24hFilter->addSample(0);
                            }
                        }
                        auto shieldedPer24h = static_cast<TxoID>(floor(m_shieldedPer24hFilter->getAverage() * 10));

                        postFunctionToClientContext([this, shieldedPer24h]()
                        {
                            m_shieldedPer24h = shieldedPer24h;
                        });
                    }
                });
            }
        }
    }

    void WalletClient::updateClientTxState()
    {
        if (auto w = m_wallet.lock(); w)
        {
            postFunctionToClientContext([this, count = w->GetUnsafeActiveTransactionsCount()]()
            {
                m_unsafeActiveTxCount = count;
            });
        }
    }

    void WalletClient::updateNotifications()
    {
        size_t count = m_notificationCenter->getUnreadCount(
            [this] (std::vector<Notification>::const_iterator first, std::vector<Notification>::const_iterator last)
            {
                auto currentLibVersion = getLibVersion();
                auto currentClientRevision = getClientRevision();
                return std::count_if(first, last,
                    [&currentLibVersion, &currentClientRevision](const auto& p)
                    {
                        if (p.m_state == Notification::State::Unread)
                        {
                            if (p.m_type == Notification::Type::WalletImplUpdateAvailable)
                            {
                                WalletImplVerInfo info;
                                if (fromByteBuffer(p.m_content, info) &&
                                    VersionInfo::Application::DesktopWallet == info.m_application &&
                                    (currentLibVersion < info.m_version ||
                                    (currentLibVersion == info.m_version && currentClientRevision < info.m_UIrevision)))
                                {
                                    return true;
                                }
                            }
                            if (p.m_type == Notification::Type::TransactionFailed)
                            {
                                return true;
                            }
                        }
                        return false;
                    });
            });
        postFunctionToClientContext([this, count]()
        {
            m_unreadNotificationsCount = count;
        });
    }

    void WalletClient::updateConnectionTrust(bool trustedConnected)
    {
        if (trustedConnected)
        {
            ++m_trustedConnectionCount;
        }
        else if (m_trustedConnectionCount)
        {
            --m_trustedConnectionCount;
        }

        postFunctionToClientContext([this, isTrusted = m_trustedConnectionCount > 0 && m_trustedConnectionCount == m_connectedNodesCount]()
        {
            m_isConnectionTrusted = isTrusted;
        });
    }

    bool WalletClient::isConnected() const
    {
        return m_connectedNodesCount > 0;
    }

    void WalletClient::getAssetInfo(const Asset::ID assetId)
    {
        m_ainfoRequests.insert(assetId);
        m_ainfoDelayed->start(0, false, [this] () {
            processAInfo();
        });
    }

    void WalletClient::onAssetChanged(ChangeAction action, Asset::ID assetId)
    {
        m_ainfoRequests.erase(assetId);
        if(const auto oasset = m_walletDB->findAsset(assetId))
        {
            onAssetInfo(assetId, *oasset);
        }
        else
        {
            WalletAsset invalid;
            onAssetInfo(assetId, invalid);
        }
    }

    void WalletClient::processAInfo ()
    {
        if (m_ainfoRequests.empty()) {
            return;
        }

        auto reqs = m_ainfoRequests;
        for(auto assetId: reqs)
        {
            if(const auto oasset = m_walletDB->findAsset(assetId))
            {
                m_ainfoRequests.erase(assetId);
                onAssetInfo(assetId, *oasset);
            }
            else
            {
                if(auto wallet = m_wallet.lock())
                {
                    wallet->ConfirmAsset(assetId);
                }
                else
                {
                    m_ainfoRequests.erase(assetId);
                    WalletAsset invalid;
                    onAssetInfo(assetId, invalid);
                }
            }
        }
    }

    void WalletClient::makeIWTCall(std::function<boost::any()>&& function, AsyncCallback<const boost::any&>&& resultCallback)
    {
        postFunctionToClientContext([result = function(), cb = std::move(resultCallback)]()
        {
            cb(result);
        });
    }

    void WalletClient::callShaderAndStartTx(beam::ByteBuffer&& shader, std::string&& args, CallShaderAndStartTxCallback&& cback)
    {
        auto smgr = _clientShaders.lock();
        if (!smgr)
        {
            assert(false);
            postFunctionToClientContext([cb = std::move(cback)]() {
                cb("unexpected: m_wallet is null", "", TxID());
            });
            return;
        }

        smgr->CallShaderAndStartTx(std::move(shader), std::move(args), args.empty() ? 0 : 1, 0, 0,
            [this, cb = std::move(cback), shaders = _clientShaders]
            (const boost::optional<TxID>& txid, boost::optional<std::string>&& result, boost::optional<std::string>&& error) {
                auto smgr = _clientShaders.lock();
                if (!smgr)
                {
                    LOG_WARNING () << "onShaderDone but empty manager. This can happen if node changed.";
                    return;
                }

                if (!cb)
                {
                    assert(false);
                    LOG_ERROR() << "onShaderDone but empty callback";
                    return;
                }

                postFunctionToClientContext(
                    [txid, res = std::move(result), err = std::move(error), cb = std::move(cb)] () {
                        cb(err ? *err : "", res ? *res : "", txid ? *txid: TxID());
                    });
        });
    }

    void WalletClient::callShaderAndStartTx(std::string&& shaderFile, std::string&& args, CallShaderAndStartTxCallback&& cback)
    {
        try
        {
            callShaderAndStartTx(fsutils::fread(shaderFile), std::move(args), std::move(cback));
        }
        catch (std::runtime_error& err)
        {
            postFunctionToClientContext([errorMsg = std::string(err.what()), cb = std::move(cback)]() {
                cb(errorMsg, "", TxID());
            });
        }
    }

    void WalletClient::callShader(beam::ByteBuffer&& shader, std::string&& args, CallShaderCallback&& cback)
    {
        auto smgr = _clientShaders.lock();
        if (!smgr)
        {
            assert(false);
            postFunctionToClientContext([cb = std::move(cback)]() {
                cb("unexpected: m_wallet is null", "", beam::ByteBuffer());
            });
            return;
        }

        smgr->CallShader(std::move(shader), std::move(args), args.empty() ? 0 : 1, 0, 0,
            [this, cb = std::move(cback), shaders = _clientShaders]
            (boost::optional<ByteBuffer>&& data, boost::optional<std::string>&& output, boost::optional<std::string>&& error) {
                auto smgr = _clientShaders.lock();
                if (!smgr)
                {
                    LOG_WARNING() << "onShaderDone but empty manager. This can happen if node changed.";
                    return;
                }

                if (!cb)
                {
                    assert(false);
                    LOG_ERROR() << "onShaderDone but empty callback";
                    return;
                }

                postFunctionToClientContext(
                    [data = std::move(data), res = std::move(output), err = std::move(error), cb = std::move(cb)] () {
                        cb(err ? *err : "", res ? *res : "", data ? *data : beam::ByteBuffer());
                    });
        });
    }

    void WalletClient::callShader(std::string&& shaderFile, std::string&& args, CallShaderCallback&& cback)
    {
        try
        {
            callShader(fsutils::fread(shaderFile), std::move(args), std::move(cback));
        }
        catch (std::runtime_error& err)
        {
            postFunctionToClientContext([errorMsg = std::string(err.what()), cb = std::move(cback)]() {
                cb(errorMsg, "", {});
            });
        }
    }

    void WalletClient::processShaderTxData(beam::ByteBuffer&& data, ProcessShaderTxDataCallback&& cback)
    {
        auto smgr = _clientShaders.lock();
        if (!smgr)
        {
            assert(false);
            postFunctionToClientContext([cb = std::move(cback)]() {
                cb("unexpected: m_wallet is null", TxID());
            });
            return;
        }

        smgr->ProcessTxData(data,
            [this, cb = std::move(cback), shaders = _clientShaders] (const boost::optional<TxID>& txid, boost::optional<std::string>&& error) {
                auto smgr = _clientShaders.lock();
                if (!smgr)
                {
                    LOG_WARNING() << "onShaderDone but empty manager. This can happen if node changed.";
                    return;
                }

                if (!cb)
                {
                    assert(false);
                    LOG_ERROR() << "onShaderDone but empty callback";
                    return;
                }

                postFunctionToClientContext(
                    [txid, err = std::move(error), cb = std::move(cb)] () {
                        cb(err ? *err : "", txid ? *txid : TxID());
                    });
        });
    }
}
