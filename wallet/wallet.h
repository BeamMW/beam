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

#include "wallet/wallet_db.h"
#include "wallet/common.h"
#include "wallet/wallet_transaction.h"
#include "core/fly_client.h"
#include "bitcoin/bitcoin_bridge.h"
#include "bitcoin/options.h"
#include "litecoin/options.h"
#include "qtum/options.h"

namespace beam::wallet
{
    class AddressExpiredException : public std::runtime_error
    {
    public:
        explicit AddressExpiredException()
            : std::runtime_error("")
        {
        }

    };

    class FailToStartSwapException : public std::runtime_error
    {
    public:
        explicit FailToStartSwapException()
            : std::runtime_error("")
        {
        }

    };

    // Interface for walelt observer. 
    struct IWalletObserver : IWalletDbObserver
    {
        // Callback for wallet sync progress. 
        // @param done - number of done tasks
        // @param total - number of total tasks
        virtual void onSyncProgress(int done, int total) = 0;
    };

    // Wallet base class. 
    // Extends FlyClient protocol for communication with own or remote node
    struct IWallet : public proto::FlyClient
    {
        using Ptr = std::shared_ptr<IWallet>;
        virtual ~IWallet() {}

        virtual void subscribe(IWalletObserver* observer) = 0;
        virtual void unsubscribe(IWalletObserver* observer) = 0;

        // TODO: Consider removing these methods from the interface
        virtual void cancel_tx(const TxID& id) = 0;
        virtual void delete_tx(const TxID& id) = 0;

        // Callback for receiving notifications on SBBS messages
        virtual void OnWalletMessage(const WalletID& peerID, SetTxParameter&&) = 0;

    };

    // Interface for sending wallet to wallet messages
    // Used as a base for SBBS and Cold walelt endpoints
    struct IWalletMessageEndpoint
    {
        using Ptr = std::shared_ptr<IWalletMessageEndpoint>;
        virtual void Send(const WalletID& peerID, const SetTxParameter& msg) = 0;
        virtual void SendEncryptedMessage(const WalletID& peerID, const ByteBuffer& msg) = 0;
    };


    class Wallet
        : public IWallet
        , public INegotiatorGateway
    {
    public:

        // Type definitions for callback functors
        using TxCompletedAction = std::function<void(const TxID& tx_id)>;
        using UpdateCompletedAction = std::function<void()>;

        Wallet(IWalletDB::Ptr walletDB, TxCompletedAction&& action = TxCompletedAction(), UpdateCompletedAction&& updateCompleted = UpdateCompletedAction());
        virtual ~Wallet();

        void SetNodeEndpoint(std::shared_ptr<proto::FlyClient::INetwork> nodeEndpoint);
        void AddMessageEndpoint(IWalletMessageEndpoint::Ptr endpoint);

        // Metods for Atomic Swaps
        // TODO: Refactor
        void initBitcoin(io::Reactor& reactor, const BitcoinOptions& options);
        void initLitecoin(io::Reactor& reactor, const LitecoinOptions& options);
        void initQtum(io::Reactor& reactor, const QtumOptions& options);
        void initSwapConditions(Amount beamAmount, Amount swapAmount, AtomicSwapCoin swapCoin, bool isBeamSide);

        TxID transfer_money(const WalletID& from, const WalletID& to, Amount amount, Amount fee = 0, bool sender = true, Height lifetime = kDefaultTxLifetime, Height responseTime = kDefaultTxResponseTime, ByteBuffer&& message = {}, bool saveReceiver = false);
        TxID transfer_money(const WalletID& from, const WalletID& to, Amount amount, Amount fee = 0, const CoinIDList& coins = {}, bool sender = true, Height lifetime = kDefaultTxLifetime, Height responseTime = kDefaultTxResponseTime, ByteBuffer&& message = {}, bool saveReceiver = false);
        TxID transfer_money(const WalletID& from, const WalletID& to, const AmountList& amountList, Amount fee = 0, const CoinIDList& coins = {}, bool sender = true, Height lifetime = kDefaultTxLifetime, Height responseTime = kDefaultTxResponseTime, ByteBuffer&& message = {}, bool saveReceiver = false);
        TxID split_coins(const WalletID& from, const AmountList& amountList, Amount fee = 0, bool sender = true, Height lifetime = kDefaultTxLifetime, Height responseTime = kDefaultTxResponseTime, ByteBuffer&& message = {});
        TxID swap_coins(const WalletID& from, const WalletID& to, Amount amount, Amount fee, AtomicSwapCoin swapCoin, Amount swapAmount, bool isBeamSide = true, Height lifetime = kDefaultTxLifetime, Height responseTime = kDefaultTxResponseTime);


        // Resets wallet state and rescans the blockchain from scratch
        void Refresh();

        // IWallet
        void subscribe(IWalletObserver* observer) override;
        void unsubscribe(IWalletObserver* observer) override;
        void cancel_tx(const TxID& txId) override;
        void delete_tx(const TxID& txId) override;

        void ProcessTransaction(wallet::BaseTransaction::Ptr tx);
        void RegisterTransactionType(wallet::TxType type, wallet::BaseTransaction::Creator creator);
        
    private:
        void RefreshTransactions();
        void ResumeTransaction(const TxDescription& tx);
        void ResumeAllTransactions();

        void OnAsyncStarted() override;
        void OnAsyncFinished() override;
        void on_tx_completed(const TxID& txID) override;

        void confirm_outputs(const std::vector<Coin>&) override;
        void confirm_kernel(const TxID&, const Merkle::Hash& kernelID, SubTxID subTxID) override;
        void get_kernel(const TxID&, const Merkle::Hash& kernelID, SubTxID subTxID) override;
        bool get_tip(Block::SystemState::Full& state) const override;
        void send_tx_params(const WalletID& peerID, SetTxParameter&&) override;
        void register_tx(const TxID& txId, Transaction::Ptr, SubTxID subTxID) override;
        void UpdateOnNextTip(const TxID&) override;
        SecondSide::Ptr GetSecondSide(const TxID& txId) const override;

        void OnWalletMessage(const WalletID& peerID, SetTxParameter&&) override;

        // FlyClient
        void OnNewTip() override;
        void OnTipUnchanged() override;
        void OnRolledBack() override;
        void get_Kdf(Key::IKdf::Ptr&) override;
        Block::SystemState::IHistory& get_History() override;
        void OnOwnedNode(const PeerID&, bool bUp) override;

        struct RequestHandler
            : public proto::FlyClient::Request::IHandler
        {
            virtual void OnComplete(Request&) override;
            IMPLEMENT_GET_PARENT_OBJ(Wallet, m_RequestHandler)
        } m_RequestHandler;

        uint32_t SyncRemains() const;
        void CheckSyncDone();
        void getUtxoProof(const Coin::ID&);
        void report_sync_progress();
        void notifySyncProgress();
        void updateTransaction(const TxID& txID);
        void UpdateOnSynced(BaseTransaction::Ptr tx);
        void UpdateOnNextTip(BaseTransaction::Ptr tx);
        void saveKnownState();
        void RequestUtxoEvents();
        void AbortUtxoEvents();
        void ProcessUtxoEvent(const proto::UtxoEvent&);
        void SetUtxoEventsHeight(Height);
        Height GetUtxoEventsHeightNext();

        BaseTransaction::Ptr getTransaction(const WalletID& myID, const SetTxParameter& msg);
        BaseTransaction::Ptr constructTransaction(const TxID& id, TxType type);
        void ProcessStoredMessages();
        bool IsNodeInSync() const;

    private:

        static const char s_szNextUtxoEvt[];

        struct SwapConditions
        {
            Amount beamAmount = 0;
            Amount swapAmount = 0;
            AtomicSwapCoin swapCoin;
            bool isBeamSide = 0;

            bool operator== (const SwapConditions& other)
            {
                return beamAmount == other.beamAmount &&
                    swapAmount == other.swapAmount &&
                    swapCoin == other.swapCoin &&
                    isBeamSide == other.isBeamSide;
            }
        };

// The following macros define
// Wallet to Node messages (requests) to get update on blockchain state
// These messages are used during the synchronization process


#define REQUEST_TYPES_Sync(macro) \
        macro(Utxo) \
        macro(Kernel) \
        macro(UtxoEvents)

        struct AllTasks {
#define THE_MACRO(type, msgOut, msgIn) struct type { static const bool b = false; };
            REQUEST_TYPES_All(THE_MACRO)
#undef THE_MACRO
        };

        struct SyncTasks :public AllTasks {
#define THE_MACRO(type) struct type { static const bool b = true; };
            REQUEST_TYPES_Sync(THE_MACRO)
#undef THE_MACRO
        };

        struct ExtraData :public AllTasks {
            struct Transaction
            {
                TxID m_TxID;
                SubTxID m_SubTxID = kDefaultSubTxID;
            };
            struct Utxo { Coin::ID m_CoinID; };
            struct Kernel
            {
                TxID m_TxID;
                SubTxID m_SubTxID = kDefaultSubTxID;
            };
            struct Kernel2
            {
                TxID m_TxID;
                SubTxID m_SubTxID = kDefaultSubTxID;
            };
        };

#define THE_MACRO(type, msgOut, msgIn) \
        struct MyRequest##type \
            :public Request##type \
            ,public boost::intrusive::set_base_hook<> \
            ,public ExtraData::type \
        { \
            typedef boost::intrusive_ptr<MyRequest##type> Ptr; \
            bool operator < (const MyRequest##type&) const; \
            virtual ~MyRequest##type() {} \
        }; \
         \
        typedef boost::intrusive::multiset<MyRequest##type> RequestSet##type; \
        RequestSet##type m_Pending##type; \
         \
        void DeleteReq(MyRequest##type& r) \
        { \
            m_Pending##type.erase(RequestSet##type::s_iterator_to(r)); \
            r.m_pTrg = NULL; \
            r.Release(); \
        } \
        void OnRequestComplete(MyRequest##type&); \
         \
        void AddReq(MyRequest##type& x) \
        { \
            m_Pending##type.insert(x); \
            x.AddRef(); \
        } \
        bool PostReqUnique(MyRequest##type& x) \
        { \
            if (!m_NodeEndpoint || m_Pending##type.end() != m_Pending##type.find(x)) \
                return false; \
            AddReq(x); \
            m_NodeEndpoint->PostRequest(x, m_RequestHandler); \
             \
            if (SyncTasks::type::b) \
                m_LastSyncTotal++; \
            return true; \
        }

        REQUEST_TYPES_All(THE_MACRO)
#undef THE_MACRO


        IWalletDB::Ptr m_WalletDB; 
        IPrivateKeyKeeper::Ptr m_KeyKeeper;
        std::shared_ptr<proto::FlyClient::INetwork> m_NodeEndpoint;

        // List of currently active (incomplete) transactions
        std::map<TxID, BaseTransaction::Ptr> m_ActiveTransactions;

        // List of transactions that are waiting for wallet to finish sync before tx update
        std::unordered_set<BaseTransaction::Ptr> m_TransactionsToUpdate;

        // List of transactions that are waiting for the next tip (new block) to arrive
        std::unordered_set<BaseTransaction::Ptr> m_NextTipTransactionToUpdate;

        // List of registered transaction creators
        std::unordered_map<wallet::TxType, wallet::BaseTransaction::Creator> m_TxCreators;

        // Functor for callback when transaction completed
        TxCompletedAction m_TxCompletedAction;

        // Functor for callback on completion of all async updates
        UpdateCompletedAction m_UpdateCompleted;

        // Number of tasks running during sync with Node
        uint32_t m_LastSyncTotal;

        uint32_t m_OwnedNodesOnline;

        std::vector<IWalletObserver*> m_subscribers;
        std::set<IWalletMessageEndpoint::Ptr> m_MessageEndpoints;

        // Counter of running transaction updates. Used by Cold wallet
        int m_AsyncUpdateCounter = 0;

        // Members for Atomic Swaps
        // TODO: Refactor this
        IBitcoinBridge::Ptr m_bitcoinBridge;
        IBitcoinBridge::Ptr m_litecoinBridge;
        IBitcoinBridge::Ptr m_qtumBridge;
        std::vector<SwapConditions> m_swapConditions;
    };
}
