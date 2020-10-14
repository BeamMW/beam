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

#include "wallet/core/common.h"
#include "wallet/core/wallet.h"
#include "wallet/core/wallet_db.h"
#include "wallet/core/wallet_network.h"
#include "wallet/core/node_network.h"
#include "wallet/core/private_key_keeper.h"
#include "wallet/core/common_utils.h"
#include "wallet_model_async.h"
#include "wallet/client/changes_collector.h"
#include "wallet/client/extensions/notifications/notification_observer.h"
#include "wallet/client/extensions/notifications/notification_center.h"
#include "wallet/client/extensions/broadcast_gateway/interface.h"
#include "wallet/client/extensions/broadcast_gateway/broadcast_msg_validator.h"
#include "wallet/client/extensions/news_channels/exchange_rate_provider.h"
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
#include "wallet/client/extensions/offers_board/swap_offers_observer.h"
#include "wallet/client/extensions/offers_board/swap_offer.h"
#include "wallet/client/extensions/offers_board/swap_offers_board.h"
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

#include <thread>
#include <atomic>

namespace beam::wallet
{
    struct WalletStatus
    {
        struct AssetStatus
        {
            Amount available = 0;
            Amount receiving = 0;
            Amount receivingIncoming = 0;
            Amount receivingChange = 0;
            Amount sending = 0;
            Amount maturing = 0;
            Amount shielded = 0;
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
        mutable std::map<Asset::ID, AssetStatus> all;
    };

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
    {
    public:
        WalletClient(IWalletDB::Ptr walletDB, const std::string& nodeAddr, io::Reactor::Ptr reactor);
        virtual ~WalletClient();

        void start( std::map<Notification::Type,bool> activeNotifications,
                    bool isSecondCurrencyEnabled = false,
                    std::shared_ptr<std::unordered_map<TxType, BaseTransaction::Creator::Ptr>> txCreators = nullptr);

        IWalletModelAsync::Ptr getAsync();
        std::string getNodeAddress() const;
        std::string exportOwnerKey(const beam::SecString& pass) const;
        bool isRunning() const;
        bool isFork1() const;
        size_t getUnsafeActiveTransactionsCount() const;
        size_t getUnreadNotificationsCount() const;
        bool isConnectionTrusted() const;
        ByteBuffer generateVouchers(uint64_t ownID, size_t count) const;
        void setCoinConfirmationsOffset(uint32_t offset);
        uint32_t getCoinConfirmationsOffset() const;

        /// INodeConnectionObserver implementation
        void onNodeConnectionFailed(const proto::NodeConnection::DisconnectReason&) override;
        void onNodeConnectedStatusChanged(bool isNodeConnected) override;

    protected:
        // Call this before derived class is destructed to ensure
        // that no virtual function calls below will result in purecall
        void stopReactor();

        using MessageFunction = std::function<void()>;

        // use this function to post function call to client's main loop
        void postFunctionToClientContext(MessageFunction&& func);

        struct DeferredBalanceUpdate
            :public io::IdleEvt
        {
            virtual void OnSchedule() override;
            IMPLEMENT_GET_PARENT_OBJ(WalletClient, m_DeferredBalanceUpdate)
        } m_DeferredBalanceUpdate;

        // Callbacks
        virtual void onStatus(const WalletStatus& status) {}
        virtual void onTxStatus(ChangeAction, const std::vector<TxDescription>& items) {}
        virtual void onSyncProgressUpdated(int done, int total) {}
        virtual void onChangeCalculated(Amount change) {}
        virtual void onShieldedCoinsSelectionCalculated(const ShieldedCoinsSelectionInfo& selectionRes) {}
        virtual void onNeedExtractShieldedCoins(bool val) {}
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
        void calcChange(Amount amount) override;
        void calcShieldedCoinSelectionInfo(Amount amount, Amount beforehandMinFee, bool isShielded = false) override;
        void getWalletStatus() override;
        void getTransactions() override;
        void getUtxosStatus() override;
        void getAddresses(bool own) override;
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
        void getSwapOffers() override;
        void publishSwapOffer(const SwapOffer& offer) override;
        void loadSwapParams() override;
        void storeSwapParams(const beam::ByteBuffer& params) override;
#endif  // BEAM_ATOMIC_SWAP_SUPPORT
        void cancelTx(const TxID& id) override;
        void deleteTx(const TxID& id) override;
        void getCoinsByTx(const TxID& txId) override;
        void saveAddress(const WalletAddress& address, bool bOwn) override;
        void generateNewAddress() override;
        void deleteAddress(const WalletID& id) override;
        void updateAddress(const WalletID& id, const std::string& name, WalletAddress::ExpirationStatus status) override;
        void activateAddress(const WalletID& id) override;
        void getAddress(const WalletID& id) override;
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

        void switchOnOffExchangeRates(bool isActive) override;
        void switchOnOffNotifications(Notification::Type type, bool isActive) override;
        
        void getNotifications() override;
        void markNotificationAsRead(const ECC::uintBig& id) override;
        void deleteNotification(const ECC::uintBig& id) override;

        void getExchangeRates() override;
        void getPublicAddress() override;

        // implement IWalletDB::IRecoveryProgress
        bool OnProgress(uint64_t done, uint64_t total) override;

        WalletStatus getStatus() const;
        std::vector<Coin> getUtxos() const;
        

        void updateClientState();
        void updateClientTxState();
        void updateNotifications();
        void updateConnectionTrust(bool trustedConnected);
        bool isConnected() const;
    private:

        std::shared_ptr<std::thread> m_thread;
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
        ShieldedCoinsSelectionInfo _shieldedCoinsSelectionResult;
    };
}
