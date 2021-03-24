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

#pragma once

#include "core/block_crypt.h"
#include "wallet/core/common.h"
#include "wallet/core/wallet.h"
#include "wallet/core/wallet_db.h"
#include "wallet/core/wallet_network.h"
#include "wallet/core/node_network.h"
#include "wallet/core/private_key_keeper.h"
#include "wallet/core/common_utils.h"
#include "wallet/core/contracts/i_shaders_manager.h"
#include "wallet_model_async.h"
#include "changes_collector.h"
#include "extensions/notifications/notification_observer.h"
#include "extensions/notifications/notification_center.h"
#include "extensions/broadcast_gateway/interface.h"
#include "extensions/broadcast_gateway/broadcast_msg_validator.h"
#include "extensions/news_channels/exchange_rate_provider.h"

#include "extensions/dex_board/dex_board.h"
#include "extensions/dex_board/dex_order.h"
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
#include "extensions/offers_board/swap_offers_observer.h"
#include "extensions/offers_board/swap_offer.h"
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

#include <thread>
#include <atomic>

namespace beam::wallet
{
#if defined(BEAM_TESTNET)
    constexpr char kBroadcastValidatorPublicKey[] = "dc3df1d8cd489c3fe990eb8b4b8a58089a7706a5fc3b61b9c098047aac2c2812";
#elif defined(BEAM_MAINNET)
    constexpr char kBroadcastValidatorPublicKey[] = "8ea783eced5d65139bbdf432814a6ed91ebefe8079395f63a13beed1dfce39da";
#else
    constexpr char kBroadcastValidatorPublicKey[] = "db617cedb17543375b602036ab223b67b06f8648de2bb04de047f485e7a9daec";
#endif
    struct WalletStatus
    {
        struct AssetStatus
        {
            AmountBig::Type available = 0U;
            AmountBig::Type receiving = 0U;
            AmountBig::Type receivingIncoming = 0U;
            AmountBig::Type receivingChange = 0U;
            AmountBig::Type sending    = 0U;
            AmountBig::Type maturing   = 0U;
            AmountBig::Type maturingMP = 0U;
            AmountBig::Type shielded   = 0U;
        };

        bool HasStatus(Asset::ID assetId) const;
        AssetStatus GetStatus(Asset::ID assetId) const; // If doesn't have status for the assetId returns an empty one
        AssetStatus GetBeamStatus() const;

        struct
        {
            Timestamp lastTime = 0;
            int done = 0;
            int total = 0;
        } update;

        Block::SystemState::ID stateID = {};
        TxoID shieldedTotalCount = std::numeric_limits<beam::TxoID>::max();
        mutable std::map<Asset::ID, AssetStatus> all;
    };

    class SwapOffersBoard;
    class Filter;

    class WalletClient
        : private IWalletObserver
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
        , private ISwapOffersObserver
#endif  // BEAM_ATOMIC_SWAP_SUPPORT
        , private IWalletModelAsync
        , private IWalletDB::IRecoveryProgress
        , private INodeConnectionObserver
        , private IExchangeRateObserver
        , private INotificationsObserver
        , private DexBoard::IObserver
    {
    public:
        WalletClient(const Rules& rules, IWalletDB::Ptr walletDB, const std::string& nodeAddr, io::Reactor::Ptr reactor);
        virtual ~WalletClient();

        void start( std::map<Notification::Type,bool> activeNotifications,
                    bool withExchangeRates = false,
                    std::shared_ptr<std::unordered_map<TxType, BaseTransaction::Creator::Ptr>> txCreators = nullptr);

        IWalletModelAsync::Ptr getAsync();
        Wallet::Ptr getWallet(); // can return null
        IShadersManager::Ptr getAppsShaders();

        std::string getNodeAddress() const;
        std::string exportOwnerKey(const beam::SecString& pass) const;
        bool isRunning() const;
        bool isFork1() const;
        size_t getUnsafeActiveTransactionsCount() const;
        size_t getUnreadNotificationsCount() const;
        bool isConnectionTrusted() const;   
        uint8_t getMPLockTimeLimit() const;
        uint32_t getMarurityProgress(const ShieldedCoin& coin) const;
        uint16_t getMaturityHoursLeft(const ShieldedCoin& coin) const;

        ByteBuffer generateVouchers(uint64_t ownID, size_t count) const;
        void setCoinConfirmationsOffset(uint32_t offset);
        uint32_t getCoinConfirmationsOffset() const;

        /// INodeConnectionObserver implementation
        void onNodeConnectionFailed(const proto::NodeConnection::DisconnectReason&) override;
        void onNodeConnectedStatusChanged(bool isNodeConnected) override;

    protected:
        // Call this before derived class is destructed to ensure
        // that no virtual function calls below will result in purecall
        void stopReactor(bool detachThread = false);

        // use this function to post function call to client's main loop
        using MessageFunction = std::function<void()>;
        void postFunctionToClientContext(MessageFunction&& func);

        // Callbacks
        virtual void onStatus(const WalletStatus& status) {}
        virtual void onTxStatus(ChangeAction, const std::vector<TxDescription>& items) {}
        virtual void onSyncProgressUpdated(int done, int total) {}
        virtual void onChangeCalculated(beam::Amount changeAsset, beam::Amount changeBeam, beam::Asset::ID assetId) {}
        virtual void onCoinsSelectionCalculated(const CoinsSelectionInfo&) {}
        virtual void onAllUtxoChanged(ChangeAction, const std::vector<Coin>& utxos) {}
        virtual void onShieldedCoinChanged(ChangeAction, const std::vector<ShieldedCoin>& items) {}
        virtual void onAddressesChanged(ChangeAction, const std::vector<WalletAddress>& addresses) {}
        virtual void onAddresses(bool own, const std::vector<WalletAddress>& addresses) {}
        virtual void onGeneratedNewAddress(const WalletAddress& walletAddr) {}
        virtual void onGetAddress(const WalletID& id, const boost::optional<WalletAddress>& address, size_t offlinePayments) {}
        virtual void onSwapParamsLoaded(const beam::ByteBuffer& params) {}
        virtual void onNewAddressFailed() {}
        virtual void onNodeConnectionChanged(bool isNodeConnected) {}
        virtual void onWalletError(ErrorType error) {}
        virtual void FailedToStartWallet() {}
        virtual void onSendMoneyVerified() {}
        virtual void onCantSendToExpired() {}
        virtual void onPaymentProofExported(const TxID& txID, const ByteBuffer& proof) {}
        virtual void onCoinsByTx(const std::vector<Coin>& coins) {}
        virtual void onAddressChecked(const std::string& addr, bool isValid) {}
        virtual void onImportRecoveryProgress(uint64_t done, uint64_t total) {}
        virtual void onNoDeviceConnected() {}
        virtual void onImportDataFromJson(bool isOk) {}
        virtual void onExportDataToJson(const std::string& data) {}
        virtual void onPostFunctionToClientContext(MessageFunction&& func) {}
        virtual void onExportTxHistoryToCsv(const std::string& data) {}
        virtual void onAssetInfo(Asset::ID assetId, const WalletAsset&) {}
        virtual void onDexOrdersChanged(ChangeAction, const std::vector<DexOrder>&) override {}
        virtual void onStopped() {}

        virtual Version getLibVersion() const;
        virtual uint32_t getClientRevision() const;
        void onExchangeRates(const std::vector<ExchangeRate>&) override {}
        void onNotificationsChanged(ChangeAction, const std::vector<Notification>&) override {}

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
        void onSwapOffersChanged(ChangeAction, const std::vector<SwapOffer>& offers) override {}
#endif
        virtual void onPublicAddress(const std::string& publicAddr) {};

    private:

        void onCoinsChanged(ChangeAction action, const std::vector<Coin>& items) override;
        void onTransactionChanged(ChangeAction action, const std::vector<TxDescription>& items) override;
        void onSystemStateChanged(const Block::SystemState::ID& stateID) override;
        void onAddressChanged(ChangeAction action, const std::vector<WalletAddress>& items) override;
        void onShieldedCoinsChanged(ChangeAction, const std::vector<ShieldedCoin>& coins) override;
        void onSyncProgress(int done, int total) override;
        void onOwnedNode(const PeerID& id, bool connected) override;

        void sendMoney(const WalletID& receiver, const std::string& comment, Amount amount, Amount fee) override;
        void sendMoney(const WalletID& sender, const WalletID& receiver, const std::string& comment, Amount amount, Amount fee) override;
        void startTransaction(TxParameters&& parameters) override;
        void syncWithNode() override;
        void calcChange(Amount amount, Amount fee, Asset::ID assetId) override;
        void calcShieldedCoinSelectionInfo(Amount amount, Amount beforehandMinFee, Asset::ID assetId, bool isShielded = false) override;
        void getWalletStatus() override;
        void getTransactions() override;
        void getTransactions(AsyncCallback<const std::vector<TxDescription>&>&& callback) override;
        void getUtxosStatus(beam::Asset::ID) override;
        void getAddresses(bool own) override;
        void getAddresses(bool own, AsyncCallback<const std::vector<WalletAddress>&>&& callback) override;
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
        void getSwapOffers() override;
        void publishSwapOffer(const SwapOffer& offer) override;
        void loadSwapParams() override;
        void storeSwapParams(const beam::ByteBuffer& params) override;
#endif  // BEAM_ATOMIC_SWAP_SUPPORT
        void getDexOrders() override;
        void publishDexOrder(const DexOrder&) override;
        void acceptDexOrder(const DexOrderID&) override;
        void cancelTx(const TxID& id) override;
        void deleteTx(const TxID& id) override;
        void getCoinsByTx(const TxID& txId) override;
        void saveAddress(const WalletAddress& address, bool bOwn) override;
        void generateNewAddress() override;
        void generateNewAddress(AsyncCallback<const WalletAddress&>&& callback) override;
        void deleteAddress(const WalletID& id) override;
        void deleteAddress(const std::string& addr) override;
        void updateAddress(const WalletID& id, const std::string& name, WalletAddress::ExpirationStatus status) override;
        void updateAddress(const std::string& token, const std::string& name, std::time_t expirationTime) override;
        void activateAddress(const WalletID& id) override;
        void getAddress(const WalletID& id) override;
        void getAddress(const WalletID& id, AsyncCallback<const boost::optional<WalletAddress>&, size_t>&& callback) override;
        void getAddress(const std::string& addr, AsyncCallback<const boost::optional<WalletAddress>&, size_t>&& callback) override;
        void saveVouchers(const ShieldedVoucherList& v, const WalletID& walletID) override;
        void setNodeAddress(const std::string& addr) override;
        void changeWalletPassword(const SecString& password) override;
        void getNetworkStatus() override;
        void rescan() override;
        void exportPaymentProof(const TxID& id) override;
        void checkAddress(const std::string& addr) override;
        void importRecovery(const std::string& path) override;
        void importDataFromJson(const std::string& data) override;
        void exportDataToJson() override;
        void exportTxHistoryToCsv() override;
        void getAssetInfo(const Asset::ID) override;
        void makeIWTCall(std::function<boost::any()>&& function, AsyncCallback<boost::any>&& resultCallback) override;
        void callShader(const std::vector<uint8_t>& shader, const std::string& args, ShaderCallback&& cback) override;

        void switchOnOffExchangeRates(bool isActive) override;
        void switchOnOffNotifications(Notification::Type type, bool isActive) override;
        
        void getNotifications() override;
        void markNotificationAsRead(const ECC::uintBig& id) override;
        void deleteNotification(const ECC::uintBig& id) override;

        void getExchangeRates() override;
        void getPublicAddress() override;

        void generateVouchers(uint64_t ownID, size_t count, AsyncCallback<const ShieldedVoucherList&>&& callback) override;

        void setMaxPrivacyLockTimeLimitHours(uint8_t limit) override;
        void getMaxPrivacyLockTimeLimitHours(AsyncCallback<uint8_t>&& callback) override;

        void getCoins(Asset::ID assetId, AsyncCallback<const std::vector<Coin>&>&& callback) override;
        void getShieldedCoins(Asset::ID assetId, AsyncCallback<const std::vector<ShieldedCoin>&>&& callback) override;

        void enableBodyRequests(bool value) override;

        // implement IWalletDB::IRecoveryProgress
        bool OnProgress(uint64_t done, uint64_t total) override;

        WalletStatus getStatus() const;
        std::vector<Coin> getUtxos(Asset::ID assetId) const;
        
        void updateStatus();
        void updateClientState(WalletStatus&&);
        void updateMaxPrivacyStats(const WalletStatus& status);
        void updateMaxPrivacyStatsImpl(const WalletStatus& status);
        void updateClientTxState();
        void updateNotifications();
        void updateConnectionTrust(bool trustedConnected);
        bool isConnected() const;
        beam::TxoID getTotalShieldedCount() const;
        const Rules& getRules() const;

    private:
        //
        // Dex
        //
        std::weak_ptr<DexBoard> _dex;

        //
        // Shaders support
        //
        IShadersManager::WeakPtr _appsShaders;   // this is used only for applications support
        IShadersManager::WeakPtr _clientShaders; // this is used internally in the wallet client (callShader method)
        ShaderCallback _clientShadersCback;

        // Asset info can be requested multiple times for the same ID
        // We collect all such events and process them in bulk at
        // the end of the libuv cycle ignoring duplicate reuqests
        void processAInfo();
        std::set<Asset::ID>  m_ainfoRequests;
        beam::io::Timer::Ptr m_ainfoDelayed;

        // Scheduled balance updae
        beam::io::Timer::Ptr m_balanceDelayed;
        void scheduleBalance();

        std::shared_ptr<std::thread> m_thread;
        const Rules& m_rules;
        IWalletDB::Ptr m_walletDB;
        io::Reactor::Ptr m_reactor;
        IWalletModelAsync::Ptr m_async;
        std::weak_ptr<NodeNetwork> m_nodeNetwork;
        std::weak_ptr<IWalletMessageEndpoint> m_walletNetwork;
        std::weak_ptr<Wallet> m_wallet;
        // broadcasting via BBS
        std::weak_ptr<IBroadcastMsgGateway> m_broadcastRouter;
        std::weak_ptr<IBroadcastListener> m_updatesProvider;
        std::weak_ptr<IBroadcastListener> m_walletUpdatesProvider;
        std::weak_ptr<ExchangeRateProvider> m_exchangeRateProvider;
        std::shared_ptr<NotificationCenter> m_notificationCenter;
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
        std::weak_ptr<SwapOffersBoard> m_offersBulletinBoard;
#endif  // BEAM_ATOMIC_SWAP_SUPPORT
        uint32_t m_connectedNodesCount;
        uint32_t m_trustedConnectionCount;
        boost::optional<ErrorType> m_walletError;
        std::string m_initialNodeAddrStr;

        struct CoinKey
        {
            typedef Coin::ID type;
            const type& operator()(const Coin& c) const { return c.m_ID; }
        };
        ChangesCollector <Coin, CoinKey> m_CoinChangesCollector;

        struct ShieldedCoinKey
        {
            typedef TxoID type;
            const type& operator()(const ShieldedCoin& c) const { return c.m_TxoID; }
        };
        ChangesCollector <ShieldedCoin, ShieldedCoinKey> m_ShieldedCoinChangesCollector;

        struct AddressKey
        {
            typedef WalletID type;
            const type& operator()(const WalletAddress& c) const { return c.m_walletID; }
        };
        ChangesCollector <WalletAddress, AddressKey> m_AddressChangesCollector;

        struct TransactionKey
        {
            typedef TxID type;
            const type& operator()(const TxDescription& c) const { return *c.GetTxID(); }
        };
        ChangesCollector <TxDescription, TransactionKey> m_TransactionChangesCollector;
        
        // these variables are accessible from UI thread
        size_t m_unsafeActiveTxCount = 0;
        size_t m_unreadNotificationsCount = 0;
        beam::Height m_currentHeight = 0;
        bool m_isConnectionTrusted = false;
        CoinsSelectionInfo m_CoinsSelectionResult;
        std::unique_ptr<Filter> m_shieldedPer24hFilter;
        beam::wallet::WalletStatus m_status;
        std::vector<std::pair<beam::Height, beam::TxoID>> m_shieldedCountHistoryPart;
        beam::TxoID m_shieldedPer24h = 0;
        uint8_t m_mpLockTimeLimit = 0;
    };
}
