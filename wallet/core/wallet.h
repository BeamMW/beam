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

#include "wallet_db.h"
#include "common.h"
#include "base_transaction.h"
#include "core/fly_client.h"
#include "node/processor.h"

namespace beam::wallet
{
    // Exceptions
    class ReceiverAddressExpiredException : public std::runtime_error
    {
    public:
        ReceiverAddressExpiredException()
            : std::runtime_error("")
        {
        }

    };

    class FailToStartSwapException : public std::runtime_error
    {
    public:
        FailToStartSwapException()
            : std::runtime_error("")
        {
        }

    };

    class FailToStartNewTransactionException : public std::runtime_error
    {
    public:
        FailToStartNewTransactionException()
            : std::runtime_error("")
        {
        }

    };

    class InvalidTransactionParametersException : public std::runtime_error
    {
    public:
        explicit InvalidTransactionParametersException(const char* message)
            : std::runtime_error(message)
        {
        }
    };

    class SenderInvalidAddressException : public InvalidTransactionParametersException
    {
    public:
        SenderInvalidAddressException()
            : InvalidTransactionParametersException("Sending from not existing or expired SBBS address")
        {
        }

    };


    void TestSenderAddress(const TxParameters& parameters, IWalletDB::Ptr walletDB);
    TxParameters ProcessReceiverAddress(const TxParameters& parameters, IWalletDB::Ptr walletDB, bool isMandatory = true);

    // Interface for wallet observer. 
    struct IWalletObserver : IWalletDbObserver
    {
        // Callback for wallet sync progress. 
        // @param done - number of done tasks
        // @param total - number of total tasks
        virtual void onSyncProgress(int done, int total) = 0;

        // Callback for wallet own(trusted) node connection
        // @param id - connected node peer id
        // @param connected - true if node has connected otherwise false
        virtual void onOwnedNode(const PeerID& id, bool connected) = 0;
    };
    
    // Interface for wallet message consumer
    struct IWalletMessageConsumer
    {
        // Callback for receiving notifications on SBBS messages
        virtual void OnWalletMessage(const WalletID& peerID, const SetTxParameter&) = 0;
    };

    // Interface for sending wallet to wallet messages
    // Used as a base for SBBS and Cold walelt endpoints
    struct IWalletMessageEndpoint
    {
        using Ptr = std::shared_ptr<IWalletMessageEndpoint>;
        virtual void Send(const WalletID& peerID, const SetTxParameter& msg) = 0;
        virtual void SendRawMessage(const WalletID& peerID, const ByteBuffer& msg) = 0;
        virtual void Listen(const WalletID&, const ECC::Scalar::Native& sk) {}
        virtual void Unlisten(const WalletID&) {}
    };

    inline constexpr char FallbackPeers[] = "FallbackPeers";

    // Extends FlyClient protocol for communication with own or remote node
    class Wallet
        : public proto::FlyClient
        , public INegotiatorGateway
        , public IWalletMessageConsumer
    {
    public:
        using Ptr = std::shared_ptr<Wallet>;

        // Type definitions for callback functors
        using TxCompletedAction = std::function<void(const TxID& tx_id)>;
        using UpdateCompletedAction = std::function<void()>;
        using TxVisitor = std::function<void (const TxID&, BaseTransaction::Ptr)>;

        Wallet(IWalletDB::Ptr walletDB, TxCompletedAction&& action = TxCompletedAction(), UpdateCompletedAction&& updateCompleted = UpdateCompletedAction());
        virtual ~Wallet();
        void CleanupNetwork();

        void SetNodeEndpoint(std::shared_ptr<proto::FlyClient::INetwork> nodeEndpoint);
        void AddMessageEndpoint(IWalletMessageEndpoint::Ptr endpoint);

        // Rescans the blockchain from scratch
        void Rescan();

        void RegisterTransactionType(TxType type, BaseTransaction::Creator::Ptr creator);

        template<typename T>
        void RegisterTransactionType(TxType type, std::shared_ptr<T> creator)
        {
            RegisterTransactionType(type, std::static_pointer_cast<BaseTransaction::Creator>(creator));
        }
        TxID StartTransaction(const TxParameters& parameters);
        bool CanCancelTransaction(const TxID& txId) const;
        void CancelTransaction(const TxID& txId);
        void DeleteTransaction(const TxID& txId);
        
        void Subscribe(IWalletObserver* observer);
        void Unsubscribe(IWalletObserver* observer);
        void ResumeAllTransactions();
        void VisitActiveTransaction(const TxVisitor& visitor);

        bool IsWalletInSync() const;

        // Count of active transactions which are not in safe state, negotiation are not finished or data is not sent to node
        size_t GetUnsafeActiveTransactionsCount() const;

        // voucher management
        void RequestVouchersFrom(const WalletID& peerID, const WalletID& myID, uint32_t nCount = 1);
        virtual void OnVouchersFrom(const WalletAddress&, const WalletID& myID, std::vector<ShieldedTxo::Voucher>&&);
        void RequestShieldedOutputsAt(Height h, std::function<void(Height, TxoID)>&& onRequestComplete);
        bool IsConnectedToOwnNode() const;

    protected:
        void SendTransactionToNode(const TxID& txId, Transaction::Ptr, SubTxID subTxID);
    private:
        void ProcessTransaction(BaseTransaction::Ptr tx);
        void ResumeTransaction(const TxDescription& tx);

        // INegotiatorGateway
        void OnAsyncStarted() override;
        void OnAsyncFinished() override;
        void on_tx_completed(const TxID& txID) override;
        void on_tx_failed(const TxID& txID) override;

        void confirm_kernel(const TxID&, const Merkle::Hash& kernelID, SubTxID subTxID) override;
        void confirm_asset(const TxID& txID, const PeerID& ownerID, SubTxID subTxID) override;
        void confirm_asset(const TxID& txID, const Asset::ID assetId, SubTxID subTxID = kDefaultSubTxID) override;
        void get_kernel(const TxID&, const Merkle::Hash& kernelID, SubTxID subTxID) override;
        bool get_tip(Block::SystemState::Full& state) const override;
        void send_tx_params(const WalletID& peerID, const SetTxParameter&) override;
        void get_shielded_list(const TxID& txId, TxoID startIndex, uint32_t count, ShieldedListCallback&& callback) override;
        void get_proof_shielded_output(const TxID& txId, const ECC::Point& serialPublic, ProofShildedOutputCallback&& callback) override;
        void register_tx(const TxID& txId, Transaction::Ptr, SubTxID subTxID) override;
        void UpdateOnNextTip(const TxID&) override;
        void get_UniqueVoucher(const WalletID& peerID, const TxID& txID, boost::optional<ShieldedTxo::Voucher>&) override;

        // IWalletMessageConsumer
        void OnWalletMessage(const WalletID& peerID, const SetTxParameter&) override;

        // FlyClient
        void OnNewTip() override;
        void OnTipUnchanged() override;
        void OnRolledBack() override;
        void get_Kdf(Key::IKdf::Ptr&) override;
        void get_OwnerKdf(Key::IPKdf::Ptr&) override;
        Block::SystemState::IHistory& get_History() override;
        void OnOwnedNode(const PeerID&, bool bUp) override;
        void OnEventsSerif(const ECC::Hash::Value&, Height) override;
        void OnNewPeer(const PeerID& id, io::Address address) override;

        struct RequestHandler
            : public proto::FlyClient::Request::IHandler
        {
            virtual void OnComplete(Request&) override;
            IMPLEMENT_GET_PARENT_OBJ(Wallet, m_RequestHandler)
        } m_RequestHandler;

        uint32_t SyncRemains() const;
        void CheckSyncDone();
        void getUtxoProof(const Coin&);
        void report_sync_progress();
        void NotifySyncProgress();
        void UpdateTransaction(const TxID& txID);
        void UpdateTransaction(BaseTransaction::Ptr tx);
        void UpdateOnSynced(BaseTransaction::Ptr tx);
        void UpdateOnNextTip(BaseTransaction::Ptr tx);
        void SaveKnownState();
        void ProcessBody(const proto::BodyBuffers& b, Height h, NodeProcessor::Recognizer& recoginzer);
        void PreprocessBlock(TxVectors::Full& block);
        void RequestBodies();
        void RequestTreasury();
        void RequestBodies(Height currentHeight, Height startHeight);
        void AbortBodiesRequests();
        void RequestEvents();
        void AbortEvents();
        void ProcessEventUtxo(const proto::Event::Utxo& utxoEvt, Height h);
        void ProcessEventUtxo(const CoinID&, Height h, Height hMaturity, bool bAdd, const Output::User& user);
        void ProcessEventAsset(const proto::Event::AssetCtl& assetCtl, Height h);
        void SetEventsHeight(Height);
        Height GetEventsHeightNext();
        void ProcessEventShieldedUtxo(const proto::Event::Shielded& shieldedEvt, Height h);
        void RequestStateSummary();

        void OnTransactionMsg(const WalletID& myID, const SetTxParameter& msg);
        BaseTransaction::Ptr ConstructTransaction(const TxID& id, TxType type);
        BaseTransaction::Ptr ConstructTransactionFromParameters(const TxParameters& parameters);

        void MakeTransactionActive(BaseTransaction::Ptr tx);
        void ProcessStoredMessages();
        bool IsNodeInSync() const;

        void SendSpecialMsg(const WalletID& peerID, SetTxParameter&);
        void OnSpecialMsg(const WalletID& myID, const SetTxParameter&);
        std::vector<BaseTransaction::Ptr> FindTxWaitingForVouchers(const WalletID& peerID) const;
        void FailTxWaitingForVouchers(const WalletID& peerID);
        void FailVoucherRequest(const WalletID& peerID, const WalletID& myID);
        void RestoreTransactionFromShieldedCoin(ShieldedCoin& coin);
        void SetTreasuryHandled(bool);

    private:

// The following macros define
// Wallet to Node messages (requests) to get update on blockchain state
// These messages are used during the synchronization process


#define REQUEST_TYPES_Sync(macro) \
        macro(Utxo) \
        macro(Kernel) \
        macro(Events) \
        macro(StateSummary) \
        macro(BodyPack) \
        macro(Body) 

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
            struct Asset
            {
                TxID m_TxID;
                SubTxID m_SubTxID = kDefaultSubTxID;
            };
            struct ProofShieldedOutp
            {
                TxID m_TxID;
                SubTxID m_SubTxID = kDefaultSubTxID;
                ProofShildedOutputCallback m_callback;
            };
            struct ShieldedList
            {
                TxID m_TxID;
                ShieldedListCallback m_callback;
            };
            struct ShieldedOutputsAt
            {
                std::function<void(Height, TxoID)> m_callback;
            };
            struct BodyPack
            {
                Height m_StartHeight;
            };
            struct Body
            {
                Height m_Height;
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
            r.m_pTrg = nullptr; \
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
        
        std::shared_ptr<proto::FlyClient::INetwork> m_NodeEndpoint;
        std::set<IWalletMessageEndpoint::Ptr> m_MessageEndpoints;

        struct VoucherManager
        {
            struct Request
            {
                struct Target :public boost::intrusive::set_base_hook<>
                {
                    typedef boost::intrusive::multiset<Target> Set;
                    WalletID m_Value;
                    bool operator < (const Target& x) const { return m_Value < x.m_Value; }
                    IMPLEMENT_GET_PARENT_OBJ(Request, m_Target)
                } m_Target;

                WalletID m_OwnAddr;
            };

            Request::Target::Set m_setTrg;

            Request* CreateIfNew(const WalletID& trg);
            void Delete(Request&);
            void DeleteAll();

            ~VoucherManager() { DeleteAll(); }

            IMPLEMENT_GET_PARENT_OBJ(Wallet, m_VoucherManager)
        } m_VoucherManager;

        struct RecognizerHandler;

        // List of registered transaction creators
        // Creators can store some objects for the transactions, 
        // so they have to be destroyed after the transactions
        std::unordered_map<wallet::TxType, wallet::BaseTransaction::Creator::Ptr> m_TxCreators;

        // List of currently active (incomplete) transactions
        std::map<TxID, BaseTransaction::Ptr> m_ActiveTransactions;

        // List of transactions that are waiting for wallet to finish sync before tx update
        std::unordered_set<BaseTransaction::Ptr> m_TransactionsToUpdate;

        // List of transactions that are waiting for the next tip (new block) to arrive
        std::unordered_set<BaseTransaction::Ptr> m_NextTipTransactionToUpdate;

        // Functor for callback when transaction completed
        TxCompletedAction m_TxCompletedAction;

        // Functor for callback on completion of all async updates
        UpdateCompletedAction m_UpdateCompleted;

        // Number of tasks running during sync with Node
        uint32_t m_LastSyncTotal;
        uint32_t m_OwnedNodesOnline;

        std::vector<IWalletObserver*> m_subscribers;

        // Counter of running transaction updates. Used by Cold wallet
        int m_AsyncUpdateCounter = 0;
        bool m_StoredMessagesProcessed = false; // this should happen only once, but not in destructor;
        NodeProcessor::Extra m_Extra = { 0 };
        bool m_IsTreasuryHandled = false;
    };
}
