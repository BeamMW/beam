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
#include "core/block_rw.h"
#include "wallet/core/common_utils.h"
#include "extensions/broadcast_gateway/broadcast_router.h"
#include "extensions/news_channels/wallet_updates_provider.h"
#include "extensions/news_channels/exchange_rate_provider.h"
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
#include "wallet/client/extensions/offers_board/swap_offers_board.h"
#endif  // BEAM_ATOMIC_SWAP_SUPPORT
#ifdef BEAM_LELANTUS_SUPPORT
#include "wallet/transactions/lelantus/push_transaction.h"
#endif // BEAM_LELANTUS_SUPPORT

#include "filter.h"

using namespace std;

namespace
{
using namespace beam;
using namespace beam::wallet;

constexpr size_t kCollectorBufferSize = 50;
constexpr size_t kShieldedPer24hFilterSize = 20;
constexpr size_t kShieldedPer24hFilterBlocksForUpdate = 144;
constexpr size_t kShieldedCountHistoryWindowSize = kShieldedPer24hFilterSize << 1;

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

    void calcShieldedCoinSelectionInfo(Amount amount, Amount beforehandMinFee, Asset::ID assetId, bool isShielded /* = false */) override
    {
        call_async(&IWalletModelAsync::calcShieldedCoinSelectionInfo, amount, beforehandMinFee, assetId, isShielded);
    }

    void getWalletStatus() override
    {
        call_async(&IWalletModelAsync::getWalletStatus);
    }

    void getTransactions() override
    {
        call_async(&IWalletModelAsync::getTransactions);
    }

    void getUtxosStatus() override
    {
        call_async(&IWalletModelAsync::getUtxosStatus);
    }

    void getAddresses(bool own) override
    {
        call_async(&IWalletModelAsync::getAddresses, own);
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

    void saveAddress(const wallet::WalletAddress& address, bool bOwn) override
    {
        call_async(&IWalletModelAsync::saveAddress, address, bOwn);
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

    void deleteAddress(const wallet::WalletID& id) override
    {
        typedef void(IWalletModelAsync::* MethodType)(const wallet::WalletID&);
        call_async((MethodType)&IWalletModelAsync::deleteAddress, id);
    }

    void deleteAddress(const std::string& addr) override
    {
        typedef void(IWalletModelAsync::* MethodType)(const std::string&);
        call_async((MethodType)&IWalletModelAsync::deleteAddress, addr);
    }

    void updateAddress(const wallet::WalletID& id, const std::string& name, WalletAddress::ExpirationStatus status) override
    {
        call_async(&IWalletModelAsync::updateAddress, id, name, status);
    }

    void activateAddress(const wallet::WalletID& id) override
    {
        call_async(&IWalletModelAsync::activateAddress, id);
    }

    void getAddress(const WalletID& id) override
    {
        typedef void(IWalletModelAsync::* MethodType)(const WalletID&);
        call_async((MethodType)&IWalletModelAsync::getAddress, id);
    }

    void getAddress(const WalletID& id, AsyncCallback <const boost::optional<WalletAddress>&, size_t> && callback) override
    {
        typedef void(IWalletModelAsync::* MethodType)(const WalletID&, AsyncCallback<const boost::optional<WalletAddress>&, size_t>&&);
        call_async((MethodType)&IWalletModelAsync::getAddress, id, std::move(callback));
    }

    void getAddress(const std::string& addr, AsyncCallback <const boost::optional<WalletAddress>&, size_t>&& callback) override
    {
        typedef void(IWalletModelAsync::* MethodType)(const std::string&, AsyncCallback<const boost::optional<WalletAddress>&, size_t>&&);
        call_async((MethodType)&IWalletModelAsync::getAddress, addr, std::move(callback));
    }

    void saveVouchers(const ShieldedVoucherList& v, const WalletID& walletID) override
    {
        call_async(&IWalletModelAsync::saveVouchers, v, walletID);
    }

    void setNodeAddress(const std::string& addr) override
    {
        call_async(&IWalletModelAsync::setNodeAddress, addr);
    }

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

    void rescan() override
    {
        call_async(&IWalletModelAsync::rescan);
    }

    void exportPaymentProof(const wallet::TxID& id) override
    {
        call_async(&IWalletModelAsync::exportPaymentProof, id);
    }

    void checkAddress(const std::string& addr) override
    {
        call_async(&IWalletModelAsync::checkAddress, addr);
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

    void getPublicAddress() override
    {
        call_async(&IWalletModelAsync::getPublicAddress);
    }

    void generateVouchers(uint64_t ownID, size_t count, AsyncCallback<ShieldedVoucherList>&& callback) override
    {
        call_async(&IWalletModelAsync::generateVouchers, ownID, count, std::move(callback));
    }

    void getAssetInfo(Asset::ID assetId) override
    {
        call_async(&IWalletModelAsync::getAssetInfo, assetId);
    }

    void makeIWTCall(std::function<boost::any()>&& function, AsyncCallback<boost::any>&& resultCallback) override
    {
        call_async(&IWalletModelAsync::makeIWTCall, std::move(function), std::move(resultCallback));
    }

    void callShader(const std::vector<uint8_t>& shader, const std::string& args, ShaderCallback&& cback) override
    {
        call_async(&IWalletModelAsync::callShader, shader, args, cback);
    }

    void setMaxPrivacyLockTimeLimitHours(uint8_t limit) override
    {
        call_async(&IWalletModelAsync::setMaxPrivacyLockTimeLimitHours, limit);
    }

    void getMaxPrivacyLockTimeLimitHours(AsyncCallback<uint8_t>&& callback) override
    {
        call_async(&IWalletModelAsync::getMaxPrivacyLockTimeLimitHours, std::move(callback));
    }

    void getCoins(Asset::ID assetId, AsyncCallback<std::vector<Coin>>&& callback) override
    {
        call_async(&IWalletModelAsync::getCoins, assetId, std::move(callback));
    }

    void getShieldedCoins(Asset::ID assetId, AsyncCallback<std::vector<ShieldedCoin>>&& callback) override
    {
        call_async(&IWalletModelAsync::getShieldedCoins, assetId, std::move(callback));
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

    WalletClient::WalletClient(const Rules& rules, IWalletDB::Ptr walletDB, const std::string& nodeAddr, io::Reactor::Ptr reactor)
        : m_rules(rules)
        , m_walletDB(walletDB)
        , m_reactor{ reactor ? reactor : io::Reactor::create() }
        , m_async{ make_shared<WalletModelBridge>(*(static_cast<IWalletModelAsync*>(this)), *m_reactor) }
        , m_connectedNodesCount(0)
        , m_trustedConnectionCount(0)
        , m_initialNodeAddrStr(nodeAddr)
        , m_CoinChangesCollector(kCollectorBufferSize, m_reactor, [this](auto action, const auto& items) { onAllUtxoChanged(action, items); })
        , m_ShieldedCoinChangesCollector(kCollectorBufferSize, m_reactor, [this](auto action, const auto& items) { onShieldedCoinChanged(action, items); })
        , m_AddressChangesCollector(kCollectorBufferSize, m_reactor, [this](auto action, const auto& items) { onAddressesChanged(action, items); })
        , m_TransactionChangesCollector(kCollectorBufferSize, m_reactor, [this](auto action, const auto& items) { onTxStatus(action, items); })
        , m_shieldedPer24hFilter(std::make_unique<Filter>(kShieldedPer24hFilterSize))
    {
        m_ainfoDelayed = io::Timer::create(*m_reactor);
        m_balanceDelayed = io::Timer::create(*m_reactor);
    }

    WalletClient::~WalletClient()
    {
        // reactor should be already stopped here, but just in case
        // this call is unsafe and may result in crash if reactor is not stopped
        assert(!m_thread && !m_reactor);
        stopReactor();
    }

    void WalletClient::stopReactor()
    {
        try
        {
            if (m_reactor)
            {
                if (m_thread)
                {
                    m_reactor->stop();
                    m_thread->join();
                    m_thread.reset();
                }
                m_reactor.reset();
            }
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...) {
            LOG_UNHANDLED_EXCEPTION();
        }
    }

    void WalletClient::postFunctionToClientContext(MessageFunction&& func)
    {
        onPostFunctionToClientContext(move(func));
    }

    /// Methods below should be called from main thread
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
        m_thread = std::make_shared<std::thread>([this, withExchangeRates, txCreators, activeNotifications]()
        {
            try
            {
                Rules::Scope scopeRules(getRules());
                io::Reactor::Scope scope(*m_reactor);

                static const unsigned LOG_ROTATION_PERIOD_SEC = 3 * 3600; // 3 hours
                static const unsigned LOG_CLEANUP_PERIOD_SEC = 120 * 3600; // 5 days
                LogRotation logRotation(*m_reactor, LOG_ROTATION_PERIOD_SEC, LOG_CLEANUP_PERIOD_SEC);

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
                auto broadcastRouter = make_shared<BroadcastRouter>(*nodeNetwork, *walletNetwork);
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
                using ExchangeRatesSubscriber = ScopedSubscriber<IExchangeRateObserver, ExchangeRateProvider>;
                auto walletUpdatesSubscriber = make_unique<WalletUpdatesSubscriber>(static_cast<INewsObserver*>(
                    m_notificationCenter.get()), walletUpdatesProvider);
                auto ratesSubscriber = make_unique<ExchangeRatesSubscriber>(
                    static_cast<IExchangeRateObserver*>(this), exchangeRateProvider);
                auto notificationsDbSubscriber = make_unique<WalletDbSubscriber>(
                    static_cast<IWalletDbObserver*>(m_notificationCenter.get()), m_walletDB);

                //
                // DEX
                //
                auto dexBoard = make_shared<DexBoard>(*broadcastRouter, this->getAsync(), *m_walletDB);
                auto dexWDBSubscriber = make_unique<WalletDbSubscriber>(static_cast<IWalletDbObserver*>(dexBoard.get()), m_walletDB);

                using DexBoardSubscriber = ScopedSubscriber<DexBoard::IObserver, DexBoard>;
                auto dexBoardSubscriber = make_unique<DexBoardSubscriber>(static_cast<DexBoard::IObserver*>(this), dexBoard);

                _dex = dexBoard;

                //
                // Shaders
                //
                auto appsShaders = IShadersManager::CreateInstance(wallet, m_walletDB, nodeNetwork);
                auto clientShaders = IShadersManager::CreateInstance(wallet, m_walletDB, nodeNetwork);
                _appsShaders = appsShaders;
                _clientShaders = clientShaders;

                nodeNetwork->tryToConnect();
                m_reactor->run_ex([&wallet, &nodeNetwork](){
                    wallet->CleanupNetwork();
                    nodeNetwork->Disconnect();
                });

                assert(appsShaders.use_count() == 1);
                appsShaders.reset();

                assert(clientShaders.use_count() == 1);
                clientShaders.reset();

                assert(walletNetwork.use_count() == 1);
                walletNetwork.reset();

                nodeNetworkSubscriber.reset();
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

    IShadersManager::Ptr WalletClient::getAppsShaders()
    {
        auto sp = _appsShaders.lock();
        return sp;
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
        auto mpAnonymitySet = packedMessage->m_MaxPrivacyMinAnonymitySet;
        return mpAnonymitySet ? us.m_Progress * 64 / mpAnonymitySet : us.m_Progress;
    }

    uint16_t WalletClient::getMaturityHoursLeft(const ShieldedCoin& coin) const
    {
        auto& timeLimit = m_mpLockTimeLimit;

        uint16_t hoursLeftByBlocksU = 0;
        if (timeLimit)
        {
            auto& stateID = m_status.stateID;
            auto hoursLeftByBlocks = (coin.m_confirmHeight + timeLimit * 60 - stateID.m_Height) / 60.;
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

    /////////////////////////////////////////////
    /// IWalletClientAsync implementation, these method are called in background thread and could safelly access wallet DB

    ByteBuffer WalletClient::generateVouchers(uint64_t ownID, size_t count) const
    {
        auto vouchers = GenerateVoucherList(m_walletDB->get_KeyKeeper(), ownID, count);
        if (vouchers.empty())
            return {};

        return toByteBuffer(vouchers);
    }

    void WalletClient::setCoinConfirmationsOffset(uint32_t offset)
    {
        m_walletDB->setCoinConfirmationsOffset(offset);
    }

    uint32_t WalletClient::getCoinConfirmationsOffset() const
    {
        return m_walletDB->getCoinConfirmationsOffset();
    }

    void WalletClient::onCoinsChanged(ChangeAction action, const std::vector<Coin>& items)
    {
        m_CoinChangesCollector.CollectItems(action, items);
        scheduleBalance();
    }

    void WalletClient::scheduleBalance()
    {
        m_balanceDelayed->start(0, false, [this] () {
            onStatus(getStatus());
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
                saveAddress(senderAddress, true); // should update the wallet_network
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
                    saveAddress(senderAddress, true); // should update the wallet_network
                
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
        const auto change = CalcChange(m_walletDB, amount, fee, assetId);
        onChangeCalculated(change.changeAsset, change.changeBeam, assetId);
    }

    void WalletClient::calcShieldedCoinSelectionInfo(Amount requested, Amount beforehandMinFee, Asset::ID assetId, bool isShielded /* = false */)
    {
        m_shieldedCoinsSelectionResult = CalcShieldedCoinSelectionInfo(m_currentHeight, m_walletDB, requested, beforehandMinFee, assetId, isShielded);
        onShieldedCoinsSelectionCalculated(m_shieldedCoinsSelectionResult);
    }

    void WalletClient::getWalletStatus()
    {
        onStatus(getStatus());
    }

    void WalletClient::getTransactions()
    {
        onTransactionChanged(ChangeAction::Reset, m_walletDB->getTxHistory(wallet::TxType::ALL));
    }

    void WalletClient::getUtxosStatus()
    {
        onCoinsChanged(ChangeAction::Reset, getUtxos(beam::Asset::s_BeamID));
        onShieldedCoinsChanged(ChangeAction::Reset, m_walletDB->getShieldedCoins(beam::Asset::s_BeamID));
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

    void WalletClient::publishDexOrder(const DexOrder& offer)
    {
        if (auto dex = _dex.lock())
        {
            try
            {
                dex->publishOrder(offer);
            }
            catch (const std::runtime_error& e)
            {
                LOG_ERROR() << e.what();
            }
        }
    }

    void WalletClient::acceptDexOrder(const DexOrderID& orderId)
    {
        if (auto dex = _dex.lock())
        {
            if (auto order = dex->getOrder(orderId))
            {
                auto params = CreateDexTransactionParams(order->sbbsID, orderId);
                startTransaction(std::move(params));
            }
        }

        /*if (auto dex = _dex.lock())
        {
            try
            {
                dex->acceptOrder(orderId);
            }
            catch (const std::runtime_error& e)
            {
                LOG_ERROR() << e.what();
            }
        }*/
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

    void WalletClient::saveAddress(const WalletAddress& address, bool bOwn)
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

    void WalletClient::deleteAddress(const WalletID& id)
    {
        try
        {
            auto pVal = m_walletDB->getAddress(id);
            if (pVal)
            {
                m_walletDB->deleteAddress(id);
            }
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...) {
            LOG_UNHANDLED_EXCEPTION();
        }
    }

    void WalletClient::deleteAddress(const std::string& addr)
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

    void WalletClient::updateAddress(const WalletID& id, const std::string& name, WalletAddress::ExpirationStatus status)
    {
        try
        {
            auto addr = m_walletDB->getAddress(id);

            if (addr)
            {
                if (addr->isOwn() &&
                    status != WalletAddress::ExpirationStatus::AsIs)
                {
                    addr->setExpiration(status);
                }
                addr->setLabel(name);
                m_walletDB->saveAddress(*addr);
            }
            else
            {
                LOG_ERROR() << "Address " << to_string(id) << " is absent.";
            }
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...) {
            LOG_UNHANDLED_EXCEPTION();
        }
    }

    void WalletClient::activateAddress(const WalletID& id)
    {
        try
        {
            auto addr = m_walletDB->getAddress(id);
            if (addr)
            {
                if (addr->isOwn())
                {
                    addr->setExpiration(WalletAddress::ExpirationStatus::OneDay);
                }
                m_walletDB->saveAddress(*addr);
            }
            else
            {
                LOG_ERROR() << "Address " << to_string(id) << " is absent.";
            }
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...) {
            LOG_UNHANDLED_EXCEPTION();
        }
    }

    void WalletClient::getAddress(const WalletID& id) 
    {
        try
        {
            onGetAddress(id, m_walletDB->getAddress(id), m_walletDB->getVoucherCount(id));
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...) {
            LOG_UNHANDLED_EXCEPTION();
        }
    }

    void WalletClient::getAddress(const WalletID& id, AsyncCallback<const boost::optional<WalletAddress>&, size_t>&& callback)
    {
        try
        {
            auto addr = m_walletDB->getAddress(id);
            size_t vouchersCount = m_walletDB->getVoucherCount(id);

            postFunctionToClientContext([addr, vouchersCount, cb = std::move(callback)]() 
            {
                cb(addr, vouchersCount);
            });
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...) {
            LOG_UNHANDLED_EXCEPTION();
        }
    }

    void WalletClient::getAddress(const std::string& addrStr, AsyncCallback<const boost::optional<WalletAddress>&, size_t>&& callback)
    {
        try
        {
            auto addr = m_walletDB->getAddress(addrStr);
            size_t vouchersCount = 0;
            if (addr && addr->m_walletID != Zero)
            {
                vouchersCount = m_walletDB->getVoucherCount(addr->m_walletID);
            }

            postFunctionToClientContext([addr, vouchersCount, cb = std::move(callback)]()
            {
                cb(addr, vouchersCount);
            });
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...) {
            LOG_UNHANDLED_EXCEPTION();
        }
    }

    void WalletClient::saveVouchers(const ShieldedVoucherList& vouchers, const WalletID& walletID)
    {
        try
        {
            if (m_walletDB->getVoucherCount(walletID) > 0)
            {
                // don't save vouchers if we already have to avoid zombie vouchers
                return;
            }
            storage::SaveVouchers(*m_walletDB, vouchers, walletID);
            
            // notify client about voucher count changes
            onGetAddress(walletID, m_walletDB->getAddress(walletID), m_walletDB->getVoucherCount(walletID));
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

    void WalletClient::checkAddress(const std::string& addr)
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
                m_walletDB->ImportRecovery(path, *w, *this);
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
        auto data = storage::ExportTxHistoryToCsv(*m_walletDB);
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

    void WalletClient::getPublicAddress()
    {
        onPublicAddress(GeneratePublicOfflineAddress(*m_walletDB));
    }

    void WalletClient::generateVouchers(uint64_t ownID, size_t count, AsyncCallback<ShieldedVoucherList>&& callback)
    {
        auto vouchers = GenerateVoucherList(m_walletDB->get_KeyKeeper(), ownID, count);
        postFunctionToClientContext([res = std::move(vouchers), cb = std::move(callback)]() 
        {
            cb(std::move(res));
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
            cb(std::move(res));
        });
    }

    void WalletClient::getCoins(Asset::ID assetId, AsyncCallback<std::vector<Coin>>&& callback)
    {
        auto coins = getUtxos(assetId);
        postFunctionToClientContext([coins = std::move(coins), cb = std::move(callback)]()
        {
            cb(std::move(coins));
        });
    }

    void WalletClient::getShieldedCoins(Asset::ID assetId, AsyncCallback<std::vector<ShieldedCoin>>&& callback)
    {
        auto coins = m_walletDB->getShieldedCoins(assetId);
        postFunctionToClientContext([coins = std::move(coins), cb = std::move(callback)]()
        {
            cb(std::move(coins));
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

    vector<Coin> WalletClient::getUtxos(Asset::ID assetId) const
    {
        vector<Coin> utxos;
        m_walletDB->visitCoins([&utxos, assetId](const Coin& c)->bool
            {
                if (c.m_ID.m_AssetID == assetId)
                    utxos.push_back(c);
                return true;
            });
        return utxos;
    }

    WalletStatus WalletClient::getStatus() const
    {
        WalletStatus status;
        storage::Totals allTotals(*m_walletDB);

        for(const auto& totalsPair: allTotals.allTotals) {
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
        onStatus(status);
        updateMaxPrivacyStats(status);
        updateClientState(std::move(status));

    }

    void WalletClient::updateClientState(WalletStatus&& status)
    {
        if (auto w = m_wallet.lock(); w)
        {
            postFunctionToClientContext([this, currentHeight = m_walletDB->getCurrentHeight()
                , count = w->GetUnsafeActiveTransactionsCount()
                , status = std::move(status)
                , limit = m_walletDB->get_MaxPrivacyLockTimeLimitHours()]()
            {
                m_status = std::move(status);
                m_currentHeight = currentHeight;
                m_unsafeActiveTxCount = count;
                m_mpLockTimeLimit = limit;
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

    void WalletClient::processAInfo ()
    {
        if (m_ainfoRequests.empty()) {
            return;
        }

        auto reqs = m_ainfoRequests;
        m_ainfoRequests.clear();

        for(auto assetId: reqs)
        {
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
    }

    void WalletClient::makeIWTCall(std::function<boost::any()>&& function, AsyncCallback<boost::any>&& resultCallback)
    {
        auto result = function();
        postFunctionToClientContext([result, cb = std::move(resultCallback)]()
        {
            cb(result);
        });
    }

    void WalletClient::callShader(const std::vector<uint8_t>& shader, const std::string& args, ShaderCallback&& cback)
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

        if (_clientShadersCback || !smgr->IsDone()) {
            postFunctionToClientContext([cb = std::move(cback)]() {
                cb("previous call is not finished", "", TxID());
            });
            return;
        }

        try
        {
            if (!shader.empty())
            {
                smgr->CompileAppShader(shader);
            }
        }
        catch(std::runtime_error& err)
        {
            postFunctionToClientContext([cb = std::move(cback), msg = err.what()]() {
                cb(msg, "", TxID());
            });
            return;
        }

        _clientShadersCback = std::move(cback);

        try
        {
            smgr->Start(args, args.empty() ? 0 : 1, *this);
        }
        catch(const std::runtime_error& err)
        {
            postFunctionToClientContext([cb = std::move(_clientShadersCback), msg = err.what()]() {
                cb(msg, "", TxID());
            });

            decltype(_clientShadersCback)().swap(_clientShadersCback);
            return;
        }
    }

    void WalletClient::onShaderDone(boost::optional<TxID> txid, boost::optional<std::string> result, boost::optional<std::string> error)
    {
        if (!_clientShadersCback)
        {
            assert(false);
            LOG_ERROR() << "onShaderDone but empty callback";
            return;
        }

        auto smgr = _clientShaders.lock();
        if (!smgr)
        {
            assert(false);
            return postFunctionToClientContext([cb = std::move(_clientShadersCback)]() {
                cb("onShaderDone but empty manager", "", TxID());
            });
        }

        postFunctionToClientContext([
                txid = std::move(txid),
                res = std::move(result),
                err = std::move(error),
                cb = std::move(_clientShadersCback)
                ] () {
                    cb(err ? *err : "", res ? *res : "", txid ? *txid: TxID());
                });
    }
}
