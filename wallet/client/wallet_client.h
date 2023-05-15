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
#include "extensions/news_channels/verification_provider.h"
#ifdef BEAM_ASSET_SWAP_SUPPORT
#include "extensions/dex_board/dex_board.h"
#include "extensions/dex_board/dex_order.h"
#endif  // BEAM_ASSET_SWAP_SUPPORT
#include "keykeeper/hid_key_keeper.h"

#ifdef BEAM_IPFS_SUPPORT
#include "wallet/ipfs/ipfs.h"
#else
#include "wallet/ipfs/ipfs_config.h"
#endif

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
#include "extensions/offers_board/swap_offers_observer.h"
#include "extensions/offers_board/swap_offer.h"
#endif

#include <thread>
#include <atomic>
#include <functional>

namespace beam
{
    class HttpClient;
}

namespace beam::wallet
{
    constexpr char SEED_PARAM_NAME[] = "SavedSeed";
#ifdef BEAM_ASSET_SWAP_SUPPORT
    constexpr char ASSET_SWAP_PARAMS_NAME[] = "LastAssetSwapParams";
#endif  // BEAM_ASSET_SWAP_SUPPORT
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
        std::set<Asset::ID> nzAssets;
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
        , private IExchangeRatesObserver
        , private INotificationsObserver
#ifdef BEAM_ASSET_SWAP_SUPPORT
        , private DexBoard::IObserver
#endif  // BEAM_ASSET_SWAP_SUPPORT
        , private IVerificationObserver
        , public wallet::HidKeyKeeper::IEvents
    {
    public:

        using OpenDBFunction = std::function<IWalletDB::Ptr()>;
        WalletClient(const Rules& rules, IWalletDB::Ptr walletDB, OpenDBFunction&& walletDBFunc, const std::string& nodeAddr, io::Reactor::Ptr reactor);
        WalletClient(const Rules& rules, IWalletDB::Ptr walletDB, const std::string& nodeAddr, io::Reactor::Ptr reactor);
        WalletClient(const Rules& rules, OpenDBFunction&& walletDBFunc, const std::string& nodeAddr, io::Reactor::Ptr reactor); // lazy DB creation ctor
        ~WalletClient() override;

        void start( std::map<Notification::Type,bool> activeNotifications,
                    bool withExchangeRates = false,
                    std::shared_ptr<std::unordered_map<TxType, BaseTransaction::Creator::Ptr>> txCreators = nullptr);

        IWalletModelAsync::Ptr getAsync();
        Wallet::Ptr getWallet(); // can return null
        IWalletDB::Ptr getWalletDB();
        NodeNetwork::Ptr getNodeNetwork(); // may return null

        #ifdef BEAM_IPFS_SUPPORT
        IPFSService::Ptr getIPFS();
        IPFSService::Ptr IWThread_startIPFSNode(); // throws on fail
        void IWThread_setIPFSConfig(asio_ipfs::config&&); // throws on fail;
        void IWThread_stopIPFSNode(); // throws on fail;
        #endif

        IShadersManager::Ptr IWThread_createAppShaders(const std::string& appid, const std::string& appname, uint32_t privilegeLvl);

        std::string getNodeAddress() const;
        std::string exportOwnerKey(const beam::SecString& pass) const;
        bool isRunning() const;
        bool isFork1() const;
        size_t getUnsafeActiveTransactionsCount() const;
        size_t getUnreadNotificationsCount() const;
        bool isConnectionTrusted() const;   
        bool isSynced() const;
        uint8_t getMPLockTimeLimit() const;
        uint32_t getMarurityProgress(const ShieldedCoin& coin) const;
        uint16_t getMaturityHoursLeft(const ShieldedCoin& coin) const;

        std::set<beam::Asset::ID> getAssetsFull() const;
        std::set<beam::Asset::ID> getAssetsNZ() const;
        beam::AmountBig::Type getAvailable(beam::Asset::ID) const;
        beam::AmountBig::Type getAvailableRegular(beam::Asset::ID) const;
        beam::AmountBig::Type getAvailableShielded(beam::Asset::ID) const;
        beam::AmountBig::Type getReceiving(beam::Asset::ID) const;
        beam::AmountBig::Type getReceivingIncoming(beam::Asset::ID) const;
        beam::AmountBig::Type getReceivingChange(beam::Asset::ID) const;
        beam::AmountBig::Type getSending(beam::Asset::ID) const;
        beam::AmountBig::Type getMaturing(beam::Asset::ID) const;
        beam::AmountBig::Type getMatutingMP(beam::Asset::ID) const;
        bool hasShielded(beam::Asset::ID) const;

        beam::Height getCurrentHeight() const;
        beam::Timestamp getCurrentHeightTimestamp() const;
        beam::Timestamp getAverageBlockTime() const;
        beam::Timestamp getLastBlockTime() const;
        beam::Block::SystemState::ID getCurrentStateID() const;

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
        virtual void onCoinsSelected(const CoinsSelectionInfo&) {}
        virtual void onNormalCoinsChanged(ChangeAction, const std::vector<Coin>& utxos) {}
        virtual void onShieldedCoinChanged(ChangeAction, const std::vector<ShieldedCoin>& items) {}
        virtual void onAddressesChanged(ChangeAction, const std::vector<WalletAddress>& addresses) {}
        virtual void onAddresses(bool own, const std::vector<WalletAddress>& addresses) {}
        virtual void onGeneratedNewAddress(const WalletAddress& walletAddr) {}
        virtual void onGetAddress(const WalletID& token, const boost::optional<WalletAddress>& address, size_t offlinePayments) {}
        virtual void onSwapParamsLoaded(const beam::ByteBuffer& params) {}
        virtual void onAssetSwapParamsLoaded(const beam::ByteBuffer& params) {}
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
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
        virtual void onExportAtomicSwapTxHistoryToCsv(const std::string& data) {}
#endif // BEAM_ATOMIC_SWAP_SUPPORT
#ifdef BEAM_ASSET_SWAP_SUPPORT
        virtual void onExportAssetsSwapTxHistoryToCsv(const std::string& data) {}
#endif  // BEAM_ASSET_SWAP_SUPPORT
        virtual void onExportContractTxHistoryToCsv(const std::string& data) {}
        virtual void onAssetInfo(Asset::ID assetId, const WalletAsset&) {}
        virtual void onStopped() {}
        virtual void onFullAssetsListLoaded() {}
        virtual void onInstantMessage(Timestamp time, const WalletID& counterpart, const std::string& message, bool isIncome) {}
        virtual void onGetChatList(const std::vector<std::pair<beam::wallet::WalletID, bool>>& chats) {}
        virtual void onGetChatMessages(const std::vector<InstantMessage>& messages) {}
        virtual void onChatRemoved(const WalletID& counterpart) {}

#ifdef BEAM_ASSET_SWAP_SUPPORT
        void onDexOrdersChanged(ChangeAction, const std::vector<DexOrder>&) override {}
        void onFindDexOrder(const DexOrder&) override {}
#endif  // BEAM_ASSET_SWAP_SUPPORT

        virtual Version getLibVersion() const;
        virtual uint32_t getClientRevision() const;
        void onExchangeRates(const ExchangeRates&) override {}
        void onNotificationsChanged(ChangeAction, const std::vector<Notification>&) override {}
        void onVerificationInfo(const std::vector<VerificationInfo>&) override {}
        virtual void onPublicAddress(const std::string& publicAddr) {}

        #ifdef BEAM_ATOMIC_SWAP_SUPPORT
        void onSwapOffersChanged(ChangeAction, const std::vector<SwapOffer>& offers) override {}
        #endif

        #ifdef BEAM_IPFS_SUPPORT
        virtual void onIPFSStatus(bool running, const std::string& error, unsigned int peercnt) {}
        #endif

    private:
        void onAssetChanged(ChangeAction action, Asset::ID assetID) override;
        void onCoinsChanged(ChangeAction action, const std::vector<Coin>& items) override;
        void onTransactionChanged(ChangeAction action, const std::vector<TxDescription>& items) override;
        void onSystemStateChanged(const Block::SystemState::ID& stateID) override;
        void onAddressChanged(ChangeAction action, const std::vector<WalletAddress>& items) override;
        void onShieldedCoinsChanged(ChangeAction, const std::vector<ShieldedCoin>& coins) override;
        void onSyncProgress(int done, int total) override;
        void onOwnedNode(const PeerID& id, bool connected) override;
        void onIMSaved(Timestamp time, const WalletID& counterpart, const std::string& message, bool isIncome) override;

        void sendMoney(const WalletID& receiver, const std::string& comment, Amount amount, Amount fee) override;
        void sendMoney(const WalletID& sender, const WalletID& receiver, const std::string& comment, Amount amount, Amount fee) override;
        void sendMoneyInternal(const WalletID* pSender, const WalletID& receiver, const std::string& comment, Amount amount, Amount fee);
        void startTransaction(TxParameters&& parameters) override;
        void syncWithNode() override;
        void calcChange(Amount amount, Amount fee, Asset::ID assetId) override;
        void selectCoins(Amount amount, Amount beforehandMinFee, Asset::ID assetId, bool isShielded) override;
        void selectCoins(Amount amount, Amount beforehandMinFee, Asset::ID assetId, bool isShielded, AsyncCallback<const CoinsSelectionInfo&>&& callback) override;
        void getWalletStatus() override;
        void getTransactions() override;
        void getTransactions(AsyncCallback<const std::vector<TxDescription>&>&& callback) override;
        void getTransactionsSmoothly() override;
        void getAllUtxosStatus() override;
        void getAddresses(bool own) override;

        #ifdef BEAM_ATOMIC_SWAP_SUPPORT
        void getSwapOffers() override;
        void publishSwapOffer(const SwapOffer& offer) override;
        void loadSwapParams() override;
        void storeSwapParams(const beam::ByteBuffer& params) override;
        void CreateSwapTxParams(Amount amount, Amount beamFee, AtomicSwapCoin swapCoin, Amount swapAmount, Amount swapFeeRate, bool isBeamSide, Height responseTime, AsyncCallback<TxParameters&&>&& callback) override;
        #endif

        #ifdef BEAM_IPFS_SUPPORT
        void setIPFSConfig(asio_ipfs::config&&) override;
        void stopIPFSNode() override;
        void startIPFSNode() override;
        #endif
        void cancelTx(const TxID& id) override;
        void deleteTx(const TxID& id) override;
        void getCoinsByTx(const TxID& txId) override;
        void saveAddress(const WalletAddress& address) override;
        void generateNewAddress() override;
        void generateNewAddress(AsyncCallback<const WalletAddress&>&& callback) override;
        void generateToken(TokenType, Amount, Asset::ID, std::string sVer, AsyncCallback<std::string&&>&& callback) override;
        void deleteAddress(const WalletID& addr) override;
        void updateAddress(const WalletID& addr, const std::string& name, WalletAddress::ExpirationStatus expirationStatus) override;
        void updateAddress(const WalletID& addr, const std::string& name, beam::Timestamp expirationTime) override;
        void activateAddress(const WalletID& addr) override;
        void getAddress(const WalletID& addr) override;
        void getAddress(const WalletID& addr, AsyncCallback<const boost::optional<WalletAddress>&, size_t>&& callback) override;
        void getAddressByToken(const std::string& token, AsyncCallback<const boost::optional<WalletAddress>&, size_t>&& callback) override;
        void deleteAddressByToken(const std::string& token) override;
        void verifyOnHw(const std::string& token) override;
        void saveVouchers(const ShieldedVoucherList& v, const WalletID& walletID) override;
        void setNodeAddress(const std::string& addr) override;
        void changeWalletPassword(const SecString& password) override;
        void getNetworkStatus() override;

#ifdef BEAM_ASSET_SWAP_SUPPORT
        void loadDexOrderParams() override;
        void storeDexOrderParams(const beam::ByteBuffer& params) override;
        void getDexOrders() override;
        void getDexOrder(const DexOrderID&) override;
        void publishDexOrder(const DexOrder&) override;
        void cancelDexOrder(const DexOrderID&) override;
#endif  // BEAM_ASSET_SWAP_SUPPORT

        virtual void loadFullAssetsList() override;

        #ifdef BEAM_IPFS_SUPPORT
        void getIPFSStatus() override;
        #endif

        void rescan() override;
        void exportPaymentProof(const TxID& id) override;
        void checkNetworkAddress(const std::string& addr) override;
        void importRecovery(const std::string& path) override;
        void importDataFromJson(const std::string& data) override;
        void exportDataToJson() override;
        void exportTxHistoryToCsv() override;
        void getAssetInfo(const Asset::ID) override;
        void makeIWTCall(std::function<boost::any()>&& function, AsyncCallback<const boost::any&>&& resultCallback) override;
        void callShader(beam::ByteBuffer&& shader, std::string&& args, CallShaderCallback&& cback) override;
        void callShader(std::string&& shaderFile, std::string&& args, CallShaderCallback&& cback) override;
        void callShaderAndStartTx(beam::ByteBuffer&& shader, std::string&& args, CallShaderAndStartTxCallback&& cback) override;
        void callShaderAndStartTx(std::string&& shaderFile, std::string&& args, CallShaderAndStartTxCallback&& cback) override;
        void processShaderTxData(beam::ByteBuffer&& data, ProcessShaderTxDataCallback&& cback) override;

        void switchOnOffExchangeRates(bool isActive) override;
        void switchOnOffNotifications(Notification::Type type, bool isActive) override;
        
        void getNotifications() override;
        void markNotificationAsRead(const ECC::uintBig& id) override;
        void deleteNotification(const ECC::uintBig& id) override;

        void getExchangeRates() override;
        void getPublicAddress() override;
        void getVerificationInfo() override;

        void setMaxPrivacyLockTimeLimitHours(uint8_t limit) override;
        void getMaxPrivacyLockTimeLimitHours(AsyncCallback<uint8_t>&& callback) override;

        void setCoinConfirmationsOffset(uint32_t val) override;
        void getCoinConfirmationsOffset(AsyncCallback<uint32_t>&& callback) override;

        void removeRawSeedPhrase() override;
        void readRawSeedPhrase(AsyncCallback<const std::string&>&& callback) override;

        void getAppsList(AppsListCallback&& callback) override;
        void markAppNotificationAsRead(const TxID& id) override;

        void enableBodyRequests(bool value) override;

        void sendInstantMessage(const WalletID& peerID, const WalletID& myID, ByteBuffer&& message) override;
        void getChats() override;
        void markIMsasRead(const std::vector<std::pair<Timestamp, WalletID>>&& ims) override;
        void getInstantMessages(const WalletID& peerID) override;
        void removeChat(const WalletID& peerID) override;

        // implement IWalletDB::IRecoveryProgress
        bool OnProgress(uint64_t done, uint64_t total) override;

        WalletStatus getStatus() const;
        void updateStatus();
        void updateClientState(const WalletStatus&);
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
#ifdef BEAM_ASSET_SWAP_SUPPORT
        std::weak_ptr<DexBoard> _dex;
#endif  // BEAM_ASSET_SWAP_SUPPORT

        //
        // Shaders support
        //
        IShadersManager::WeakPtr _appsShaders;   // this is used only for applications support
        IShadersManager::WeakPtr _clientShaders; // this is used internally in the wallet client (callShader method)

        // Asset info can be requested multiple times for the same ID
        // We collect all such events and process them in bulk at
        // the end of the libuv cycle ignoring duplicate reuqests
        void processAInfo();
        std::set<Asset::ID>  m_ainfoRequests;
        beam::io::Timer::Ptr m_ainfoDelayed;

        // Scheduled balance updae
        beam::io::Timer::Ptr m_balanceDelayed;
        void scheduleBalance();

        std::shared_ptr<MyThread> m_thread;
        const Rules& m_rules;
        IWalletDB::Ptr m_walletDB;
        io::Reactor::Ptr m_reactor;
        IWalletModelAsync::Ptr m_async;
        std::weak_ptr<NodeNetwork> m_nodeNetwork;
        std::weak_ptr<IWalletMessageEndpoint> m_walletNetwork;
        std::weak_ptr<Wallet> m_wallet;

        #ifdef BEAM_IPFS_SUPPORT
        std::weak_ptr<IPFSService> m_ipfs;
        boost::optional<asio_ipfs::config> m_ipfsConfig;
        uint32_t m_ipfsPeerCnt = 0;
        std::string m_ipfsError;
        #endif

        // broadcasting via BBS
        std::weak_ptr<IBroadcastMsgGateway> m_broadcastRouter;
        std::weak_ptr<IBroadcastListener> m_updatesProvider;
        std::weak_ptr<IBroadcastListener> m_walletUpdatesProvider;
        std::weak_ptr<ExchangeRateProvider> m_exchangeRateProvider;
        std::weak_ptr<VerificationProvider> m_verificationProvider;
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
            typedef std::string type;
            const type& operator()(const WalletAddress& c) const { return c.m_Token; }
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
        bool m_isSynced = false;
        std::unique_ptr<Filter> m_shieldedPer24hFilter;
        beam::wallet::WalletStatus m_status;
        std::vector<std::pair<beam::Height, beam::TxoID>> m_shieldedCountHistoryPart;
        beam::TxoID m_shieldedPer24h = 0;
        uint8_t m_mpLockTimeLimit = 0;
        OpenDBFunction m_openDBFunc;
        std::unique_ptr<HttpClient> m_httpClient;
        beam::Timestamp m_averageBlockTime = 0;
        beam::Timestamp m_lastBlockTime = 0;
        std::set<Asset::ID> m_assetsFullList = { Asset::s_BeamID };
    };
}
