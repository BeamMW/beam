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
#include "wallet/core/private_key_keeper.h"
#include "wallet_model_async.h"
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
#include "wallet/transactions/swaps/swap_offers_board.h"
#endif

#include <thread>
#include <atomic>

namespace beam::wallet
{
    struct WalletStatus
    {
        Amount available = 0;
        Amount receiving = 0;
        Amount receivingIncoming = 0;
        Amount receivingChange = 0;
        Amount sending = 0;
        Amount maturing = 0;

        struct
        {
            Timestamp lastTime = 0;
            int done = 0;
            int total = 0;
        } update;

        Block::SystemState::ID stateID = {};
    };

    class WalletClient
        : private IWalletObserver
        , private ISwapOffersObserver
        , private IWalletModelAsync
        , private IWalletDB::IRecoveryProgress
        , private IPrivateKeyKeeper::Handler
    {
    public:
        WalletClient(IWalletDB::Ptr walletDB, const std::string& nodeAddr, io::Reactor::Ptr reactor, IPrivateKeyKeeper::Ptr keyKeeper);
        virtual ~WalletClient();

        void start(std::shared_ptr<std::unordered_map<TxType, BaseTransaction::Creator::Ptr>> txCreators = nullptr);

        IWalletModelAsync::Ptr getAsync();
        std::string getNodeAddress() const;
        std::string exportOwnerKey(const beam::SecString& pass) const;
        bool isRunning() const;
        bool isFork1() const;
        size_t getUnsafeActiveTransactionsCount() const;
        bool isConnectionTrusted() const;

    protected:
        // Call this before derived class is destructed to ensure
        // that no virtual function calls below will result in purecall
        void stopReactor();

        using MessageFunction = std::function<void()>;

        // use this function to post function call to client's main loop
        void postFunctionToClientContext(MessageFunction&& func);

        virtual void onStatus(const WalletStatus& status) = 0;
        virtual void onTxStatus(ChangeAction, const std::vector<TxDescription>& items) = 0;
        virtual void onSyncProgressUpdated(int done, int total) = 0;
        virtual void onChangeCalculated(Amount change) = 0;
        virtual void onAllUtxoChanged(ChangeAction, const std::vector<Coin>& utxos) = 0;
        virtual void onAddressesChanged(ChangeAction, const std::vector<WalletAddress>& addresses) = 0;
        virtual void onAddresses(bool own, const std::vector<WalletAddress>& addresses) = 0;
        virtual void onGeneratedNewAddress(const WalletAddress& walletAddr) = 0;
        virtual void onSwapParamsLoaded(const beam::ByteBuffer& params) = 0;
        virtual void onNewAddressFailed() = 0;
        virtual void onChangeCurrentWalletIDs(WalletID senderID, WalletID receiverID) = 0;
        virtual void onNodeConnectionChanged(bool isNodeConnected) = 0;
        virtual void onWalletError(ErrorType error) = 0;
        virtual void FailedToStartWallet() = 0;
        virtual void onSendMoneyVerified() = 0;
        virtual void onCantSendToExpired() = 0;
        virtual void onPaymentProofExported(const TxID& txID, const ByteBuffer& proof) = 0;
        virtual void onCoinsByTx(const std::vector<Coin>& coins) = 0;
        virtual void onAddressChecked(const std::string& addr, bool isValid) = 0;
        virtual void onImportRecoveryProgress(uint64_t done, uint64_t total) = 0;
        virtual void onNoDeviceConnected() = 0;
        virtual void onImportDataFromJson(bool isOk) = 0;
        virtual void onExportDataToJson(const std::string& data) = 0;
        virtual void onPostFunctionToClientContext(MessageFunction&& func) = 0;
        virtual void onExportTxHistoryToCsv(const std::string& data) = 0;

    private:

        void onCoinsChanged(ChangeAction action, const std::vector<Coin>& items) override;
        void onTransactionChanged(ChangeAction action, const std::vector<TxDescription>& items) override;
        void onSystemStateChanged(const Block::SystemState::ID& stateID) override;
        void onAddressChanged(ChangeAction action, const std::vector<WalletAddress>& items) override;
        void onSyncProgress(int done, int total) override;
        void onOwnedNode(const PeerID& id, bool connected) override;

        void sendMoney(const WalletID& receiver, const std::string& comment, Amount&& amount, Amount&& fee) override;
        void sendMoney(const WalletID& sender, const WalletID& receiver, const std::string& comment, Amount&& amount, Amount&& fee) override;
        void startTransaction(TxParameters&& parameters) override;
        void syncWithNode() override;
        void calcChange(Amount&& amount) override;
        void getWalletStatus() override;
        void getTransactions() override;
        void getUtxosStatus() override;
        void getAddresses(bool own) override;
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
        void getSwapOffers() override;
        void publishSwapOffer(const SwapOffer& offer) override;
        void loadSwapParams() override;
        void storeSwapParams(const beam::ByteBuffer& params) override;
#endif
        void cancelTx(const TxID& id) override;
        void deleteTx(const TxID& id) override;
        void getCoinsByTx(const TxID& txId) override;
        void saveAddress(const WalletAddress& address, bool bOwn) override;
        void changeCurrentWalletIDs(const WalletID& senderID, const WalletID& receiverID) override;
        void generateNewAddress() override;
        void deleteAddress(const WalletID& id) override;
        void updateAddress(const WalletID& id, const std::string& name, WalletAddress::ExpirationStatus status) override;
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

        // implement IWalletDB::IRecoveryProgress
        bool OnProgress(uint64_t done, uint64_t total) override;

        WalletStatus getStatus() const;
        std::vector<Coin> getUtxos() const;

        void nodeConnectionFailed(const proto::NodeConnection::DisconnectReason&);
        void nodeConnectedStatusChanged(bool isNodeConnected);
        void updateClientState();
        void updateClientTxState();
        void updateConnectionTrust(bool trustedConnected);
        bool isConnected() const;
    private:

        template<typename T, typename KeyFunc>
        class ChangesCollector
        {
            using FlushFunc = std::function<void(ChangeAction, const std::vector<T>&)>;
            
            struct Comparator
            {
                bool operator()(const T& left, const T& right) const
                {
                    return KeyFunc()(left) < KeyFunc()(right);
                }
            };

            using ItemsList = std::set<T, Comparator>;
        public:
            ChangesCollector(size_t bufferSize, io::Reactor::Ptr reactor, FlushFunc&& flushFunc)
                : m_BufferSize(bufferSize)
                , m_FlushTimer(io::Timer::create(*reactor))
                , m_FlushFunc(std::move(flushFunc))
            {
            }

            void CollectItems(ChangeAction action, const std::vector<T>& items)
            {
                if (action == ChangeAction::Reset)
                {
                    m_NewItems.clear();
                    m_UpdatedItems.clear();
                    m_RemovedItems.clear();
                    m_FlushFunc(action, items);
                    return;
                }

                for (const auto& item : items)
                {
                    CollectItem(action, item);
                }
            }

            void Flush()
            {
                Flush(ChangeAction::Added, m_NewItems);
                Flush(ChangeAction::Updated, m_UpdatedItems);
                Flush(ChangeAction::Removed, m_RemovedItems);
            }

        private:

            void CollectItem(ChangeAction action, const T& item)
            {
                m_FlushTimer->cancel();
                m_FlushTimer->start(100, false, [this]() { Flush(); });
                switch (action)
                {
                case ChangeAction::Added:
                    AddItem(item);
                    break;
                case ChangeAction::Updated:
                    UpdateItem(item);
                    break;
                case ChangeAction::Removed:
                    RemoveItem(item);
                    break;
                default:
                    break;
                }

                if (GetChangesCount() > m_BufferSize)
                {
                    Flush();
                }
            }

            void Flush(ChangeAction action, ItemsList& items)
            {
                if (!items.empty())
                {
                    m_FlushFunc(action, std::vector<T>(items.begin(), items.end()));
                    items.clear();
                }
            }

            size_t GetChangesCount() const
            {
                return m_NewItems.size() + m_UpdatedItems.size() + m_RemovedItems.size();
            }

            void AddItem(const T& item)
            {
                // if it was removed do nothing
                if (m_RemovedItems.find(item) != m_RemovedItems.end())
                {
                    return;
                }
                m_NewItems.insert(item);
            }

            void UpdateItem(const T& item)
            {
                // if it was removed do nothing
                if (m_RemovedItems.find(item) != m_RemovedItems.end())
                {
                    return;
                }
                // if it is not in new add it to updated
                auto it = m_NewItems.find(item);
                if (it == m_NewItems.end())
                {
                    auto p = m_UpdatedItems.insert(item);
                    if (p.second == false)
                    {
                        UpdateItemValue(item, p.first, m_UpdatedItems);
                    }
                }
                else
                {
                    UpdateItemValue(item, it, m_NewItems);
                }
            }

            void RemoveItem(const T& item)
            {
                // if it is in new erase it
                if (auto it = m_NewItems.find(item); it != m_NewItems.end())
                {
                    m_NewItems.erase(it);
                    return;
                }
                // else if it is in updated remove and add to removed
                if (auto it = m_UpdatedItems.find(item); it != m_UpdatedItems.end())
                {
                    m_UpdatedItems.erase(it);
                }
                // add to removed
                m_RemovedItems.insert(item);
            }

            void UpdateItemValue(const T& item, typename ItemsList::iterator& it, ItemsList& items)
            {
                // MacOS doesn't support 'extract'
                //auto node = items.extract(it);
                //node.value() = item;
                //items.insert(std::move(node));
                items.erase(it);
                items.insert(item);
            }

        private:
            size_t m_BufferSize;
            io::Timer::Ptr m_FlushTimer;
            FlushFunc m_FlushFunc;

            ItemsList m_NewItems;
            ItemsList m_UpdatedItems;
            ItemsList m_RemovedItems;
        };

        std::shared_ptr<std::thread> m_thread;
        IWalletDB::Ptr m_walletDB;
        io::Reactor::Ptr m_reactor;
        IWalletModelAsync::Ptr m_async;
        std::weak_ptr<proto::FlyClient::INetwork> m_nodeNetwork;
        std::weak_ptr<IWalletMessageEndpoint> m_walletNetwork;
        std::weak_ptr<Wallet> m_wallet;
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
        std::weak_ptr<SwapOffersBoard> m_offersBulletinBoard;
#endif
        uint32_t m_connectedNodesCount;
        uint32_t m_trustedConnectionCount;
        boost::optional<ErrorType> m_walletError;
        std::string m_nodeAddrStr;
        IPrivateKeyKeeper::Ptr m_keyKeeper;

        struct CoinKey
        {
            typedef Coin::ID type;
            const Coin::ID& operator()(const Coin& c) const { return c.m_ID; }
        };
        ChangesCollector <Coin, CoinKey> m_CoinChangesCollector;

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
        
        // these variables is accessible from UI thread
        size_t m_unsafeActiveTxCount = 0;
        beam::Height m_currentHeight = 0;
        bool m_isConnectionTrusted = false;
    };
}
