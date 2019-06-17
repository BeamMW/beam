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

#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wdelete-non-virtual-dtor"
#endif

#include <boost/optional.hpp>

#if defined(__clang__)
#  pragma clang diagnostic pop
#endif

#include "core/common.h"
#include "core/ecc_native.h"
#include "wallet/common.h"
#include "utility/io/address.h"
#include "secstring.h"

struct sqlite3;

namespace beam::wallet
{
    const uint32_t EmptyCoinSession = 0;

    // Describes a UTXO in the context of the Wallet
    struct Coin
    {
        // Status is not stored in the database but can be
        // deduced from the current blockchain state
        enum Status
        {
            Unavailable, // initial status of a new UTXO
            Available,   // UTXO is currently present in the chain and can be spent
            Maturing,    // UTXO is present in the chain has maturity higher than current height (i.e coinbase or treasury)
            Outgoing,    // Available and participates in outgoing transaction
            Incoming,    // Outputs of incoming transaction, currently unavailable
            ChangeV0,    // deprecated.
            Spent,       // UTXO that was spent. Stored in wallet database until reset or restore

            count
        };

        Coin(Amount amount = 0, Key::Type keyType = Key::Type::Regular);
        bool operator==(const Coin&) const;
        bool operator!=(const Coin&) const;
        bool isReward() const;
        std::string toStringID() const;
        Amount getAmount() const;

        typedef Key::IDV ID; // unique identifier for the coin (including value), can be used to create blinding factor 
        ID m_ID;

        Status m_status;        // current status of the coin
        Height m_maturity;      // coin can be spent only when chain is >= this value. Valid for confirmed coins (Available, Outgoing, Incoming, Change, Spent, Maturing).

                                // The following fields are used to derive the status of the transaction
        Height m_confirmHeight; // height at which the coin was confirmed (appeared in the chain)
        Height m_spentHeight;   // height at which the coin was spent

        boost::optional<TxID> m_createTxId;  // id of the transaction which created the UTXO
        boost::optional<TxID> m_spentTxId;   // id of the transaction which spernt the UTXO
        
        uint64_t m_sessionId;   // Used in the API to lock coins for specific session (see https://github.com/BeamMW/beam/wiki/Beam-wallet-protocol-API#tx_split)

        bool IsMaturityValid() const; // is/was the UTXO confirmed?
        Height get_Maturity() const; // would return MaxHeight unless the UTXO was confirmed
        
        std::string getStatusString() const;
        static boost::optional<Coin::ID> FromString(const std::string& str);
    };

    using CoinIDList = std::vector<Coin::ID>;

    
    // Used for SBBS Address management in the wallet
    struct WalletAddress
    {
        WalletID m_walletID;
        std::string m_label;
        std::string m_category;
        Timestamp m_createTime;
        uint64_t  m_duration; // if it equals 0 then address never expires
        uint64_t  m_OwnID; // set for own address
        
        WalletAddress();
        bool isExpired() const;
        Timestamp getCreateTime() const;
        Timestamp getExpirationTime() const;

        void setLabel(const std::string& label);
        void makeExpired();
        void makeActive(uint64_t duration);
        void makeEternal();
    };

    // Describes structure of generic transaction parameter
    struct TxParameter
    {
        TxID m_txID;
        int m_subTxID = static_cast<int>(kDefaultSubTxID);
        int m_paramID;
        ByteBuffer m_value;
    };

    // Outgoing wallet messages sent through SBBS (used in Cold Wallet)
    // TODO: Think about renaming to OutgoingWalletMessage
    struct WalletMessage
    {
        int m_ID;
        WalletID m_PeerID;
        ByteBuffer m_Message;
    };

    // Used for storing incoming SBBS messages before they can be processed (used in Cold Wallet)
    struct IncomingWalletMessage
    {
        int m_ID;
        BbsChannel m_Channel;
        ByteBuffer m_Message;
    };

    // Notifications for all collection changes
    enum class ChangeAction
    {
        Added,
        Removed,
        Updated,
        Reset
    };

    class CannotGenerateSecretException : public std::runtime_error
    {
    public:
        explicit CannotGenerateSecretException()
            : std::runtime_error("")
        {
        }

    };

    struct IWalletDbObserver
    {
        virtual void onCoinsChanged() {};
        virtual void onTransactionChanged(ChangeAction action, std::vector<TxDescription>&& items) {};
        virtual void onSystemStateChanged() {};
        virtual void onAddressChanged(ChangeAction action, const std::vector<WalletAddress>& items) {};
    };

    struct IWalletDB
    {
        using Ptr = std::shared_ptr<IWalletDB>;
        virtual ~IWalletDB() {}

        // Returns the Master Key Derivative Function (operates on secret keys)
        virtual beam::Key::IKdf::Ptr get_MasterKdf() const = 0;

        // Returns the Child Key Derivative Function (operates on secret keys)
        beam::Key::IKdf::Ptr get_ChildKdf(Key::Index) const;
		beam::Key::IKdf::Ptr get_ChildKdf(const Key::IDV&) const;

        // Calculates blinding factor and commitment of specifc Coin::ID
        void calcCommitment(ECC::Scalar::Native& sk, ECC::Point& comm, const Coin::ID&);

		// import blockchain recovery data (all at once)
		// should be used only upon creation on 'clean' wallet. Throws exception on error
		void ImportRecovery(const std::string& path);

		struct IRecoveryProgress
		{
			virtual bool OnProgress(uint64_t done, uint64_t total) { return true; } // return false to stop recovery
		};

		// returns false if callback asked to stop verification.
		bool ImportRecovery(const std::string& path, IRecoveryProgress&);

        // Allocates new Key ID, used for generation of the blinding factor
        // Will return the next id starting from a random base created during wallet initialization
        virtual uint64_t AllocateKidRange(uint64_t nCount) = 0;


        // Selects a list of coins matching certain specified amount
        // Selection logic will optimize for number of UTXOs and minimize change
        // Uses greedy algorithm up to a point and follows by some heuristics
        virtual std::vector<Coin> selectCoins(Amount amount) = 0;

        // Some getters to get lists of coins by some input parameters
        virtual std::vector<Coin> getCoinsCreatedByTx(const TxID& txId) = 0;
        virtual std::vector<Coin> getCoinsByTx(const TxID& txId) = 0;
        virtual std::vector<Coin> getCoinsByID(const CoinIDList& ids) = 0;

        // Creates a shared (multisigned) coin with specific amount
        // Used in Atomic Swaps
        virtual Coin generateSharedCoin(Amount amount) = 0;

        // Set of basic coin related database methods
        virtual void store(Coin& coin) = 0;
        virtual void store(std::vector<Coin>&) = 0;
        virtual void save(const Coin& coin) = 0;
        virtual void save(const std::vector<Coin>& coins) = 0;
        virtual void remove(const Coin::ID&) = 0;
        virtual void remove(const std::vector<Coin::ID>&) = 0;
        virtual bool find(Coin& coin) = 0;
        virtual void clear() = 0;

        // Generic visitor to iterate over coin collection
        virtual void visit(std::function<bool(const Coin& coin)> func) = 0;

        // Used in split API for session management
        virtual bool lock(const CoinIDList& list, uint64_t session) = 0;
        virtual bool unlock(uint64_t session) = 0;
        virtual CoinIDList getLocked(uint64_t session) const = 0;

        // Set of methods for low level database manipulation
        virtual void setVarRaw(const char* name, const void* data, size_t size) = 0;
        virtual bool getVarRaw(const char* name, void* data, int size) const = 0;

        virtual void setPrivateVarRaw(const char* name, const void* data, size_t size) = 0;
        virtual bool getPrivateVarRaw(const char* name, void* data, int size) const = 0;

        // TODO: Consider refactoring
        virtual bool getBlob(const char* name, ByteBuffer& var) const = 0;

        // Returns currently known blockchain height
        virtual Height getCurrentHeight() const = 0;

        // Rollback UTXO set to known height (used in rollback scenario)
        virtual void rollbackConfirmedUtxo(Height minHeight) = 0;


        // /////////////////////////////////////////////
        // Transaction management
        virtual std::vector<TxDescription> getTxHistory(wallet::TxType txType = wallet::TxType::Simple, uint64_t start = 0, int count = std::numeric_limits<int>::max()) = 0;
        virtual boost::optional<TxDescription> getTx(const TxID& txId) = 0;
        virtual void saveTx(const TxDescription& p) = 0;
        virtual void deleteTx(const TxID& txId) = 0;
        virtual bool setTxParameter(const TxID& txID, SubTxID subTxID, TxParameterID paramID,
            const ByteBuffer& blob, bool shouldNotifyAboutChanges) = 0;
        virtual bool getTxParameter(const TxID& txID, SubTxID subTxID, TxParameterID paramID, ByteBuffer& blob) const = 0;
        virtual void rollbackTx(const TxID& txId) = 0;

        // ////////////////////////////////////////////
        // Address management
        virtual std::vector<WalletAddress> getAddresses(bool own) const = 0;
        virtual void saveAddress(const WalletAddress&) = 0;
        virtual void setExpirationForAllAddresses(uint64_t expiration) = 0;
        virtual boost::optional<WalletAddress> getAddress(const WalletID&) const = 0;
        virtual void deleteAddress(const WalletID&) = 0;

        // 
        virtual Timestamp getLastUpdateTime() const = 0;
        virtual void setSystemStateID(const Block::SystemState::ID& stateID) = 0;
        virtual bool getSystemStateID(Block::SystemState::ID& stateID) const = 0;

        virtual void subscribe(IWalletDbObserver* observer) = 0;
        virtual void unsubscribe(IWalletDbObserver* observer) = 0;

        virtual void changePassword(const SecString& password) = 0;

        // Block History management, used in FlyClient
        virtual Block::SystemState::IHistory& get_History() = 0;
        virtual void ShrinkHistory() = 0;

        
        // ///////////////////////////////
        // Message management
        virtual std::vector<WalletMessage> getWalletMessages() const = 0;
        virtual uint64_t saveWalletMessage(const WalletMessage& message) = 0;
        virtual void deleteWalletMessage(uint64_t id) = 0;

        virtual std::vector<IncomingWalletMessage> getIncomingWalletMessages() const = 0;
        virtual uint64_t saveIncomingWalletMessage(BbsChannel channel, const ByteBuffer& message) = 0;
        virtual void deleteIncomingWalletMessage(uint64_t id) = 0;
    };

    namespace sqlite
    {
        struct Statement;
        struct Transaction;
    }

    class WalletDB : public IWalletDB
    {
    public:
        static bool isInitialized(const std::string& path);
        static Ptr init(const std::string& path, const SecString& password, const ECC::NoLeak<ECC::uintBig>& secretKey, io::Reactor::Ptr reactor, bool separateDBForPrivateData = false);
        static Ptr open(const std::string& path, const SecString& password, io::Reactor::Ptr reactor);

        WalletDB(sqlite3* db, io::Reactor::Ptr reactor, sqlite3* sdb);
        WalletDB(sqlite3* db, const ECC::NoLeak<ECC::uintBig>& secretKey, io::Reactor::Ptr reactor, sqlite3* sdb);
        ~WalletDB();

        beam::Key::IKdf::Ptr get_MasterKdf() const override;
        uint64_t AllocateKidRange(uint64_t nCount) override;
        std::vector<Coin> selectCoins(Amount amount) override;
        std::vector<Coin> getCoinsCreatedByTx(const TxID& txId) override;
        std::vector<Coin> getCoinsByTx(const TxID& txId) override;
        std::vector<Coin> getCoinsByID(const CoinIDList& ids) override;
        Coin generateSharedCoin(Amount amount) override;
        void store(Coin& coin) override;
        void store(std::vector<Coin>&) override;
        void save(const Coin& coin) override;
        void save(const std::vector<Coin>& coins) override;
        void remove(const Coin::ID&) override;
        void remove(const std::vector<Coin::ID>&) override;
        bool find(Coin& coin) override;
        void clear() override;

        void visit(std::function<bool(const Coin& coin)> func) override;

        void setVarRaw(const char* name, const void* data, size_t size) override;
        bool getVarRaw(const char* name, void* data, int size) const override;

        void setPrivateVarRaw(const char* name, const void* data, size_t size) override;
        bool getPrivateVarRaw(const char* name, void* data, int size) const override;

        bool getBlob(const char* name, ByteBuffer& var) const override;
        Height getCurrentHeight() const override;
        void rollbackConfirmedUtxo(Height minHeight) override;

        std::vector<TxDescription> getTxHistory(wallet::TxType txType, uint64_t start, int count) override;
        boost::optional<TxDescription> getTx(const TxID& txId) override;
        void saveTx(const TxDescription& p) override;
        void deleteTx(const TxID& txId) override;
        void rollbackTx(const TxID& txId) override;

        std::vector<WalletAddress> getAddresses(bool own) const override;
        void saveAddress(const WalletAddress&) override;
        void setExpirationForAllAddresses(uint64_t expiration) override;
        boost::optional<WalletAddress> getAddress(const WalletID&) const override;
        void deleteAddress(const WalletID&) override;

        Timestamp getLastUpdateTime() const override;
        void setSystemStateID(const Block::SystemState::ID& stateID) override;
        bool getSystemStateID(Block::SystemState::ID& stateID) const override;

        void subscribe(IWalletDbObserver* observer) override;
        void unsubscribe(IWalletDbObserver* observer) override;

        void changePassword(const SecString& password) override;

        bool setTxParameter(const TxID& txID, SubTxID subTxID, TxParameterID paramID,
            const ByteBuffer& blob, bool shouldNotifyAboutChanges) override;
        bool getTxParameter(const TxID& txID, SubTxID subTxID, TxParameterID paramID, ByteBuffer& blob) const override;

        Block::SystemState::IHistory& get_History() override;
        void ShrinkHistory() override;

        bool lock(const CoinIDList& list, uint64_t session) override;
        bool unlock(uint64_t session) override;
        CoinIDList getLocked(uint64_t session) const override;

        std::vector<WalletMessage> getWalletMessages() const override;
        uint64_t saveWalletMessage(const WalletMessage& message) override;
        void deleteWalletMessage(uint64_t id) override;

        std::vector<IncomingWalletMessage> getIncomingWalletMessages() const override;
        uint64_t saveIncomingWalletMessage(BbsChannel channel, const ByteBuffer& message) override;
        void deleteIncomingWalletMessage(uint64_t id) override;

    private:
        void removeImpl(const Coin::ID& cid);
        void notifyCoinsChanged();
        void notifyTransactionChanged(ChangeAction action, std::vector<TxDescription>&& items);
        void notifySystemStateChanged();
        void notifyAddressChanged(ChangeAction action, const std::vector<WalletAddress>& items);

        static uint64_t get_RandomID();
        bool updateRaw(const Coin&);
        void insertRaw(const Coin&);
        void insertNew(Coin&);
        void saveRaw(const Coin&);

        // ////////////////////////////////////////
        // Cache for optimized access for database fields
        using ParameterCache = std::map<TxID, std::map<SubTxID, std::map<TxParameterID, boost::optional<ByteBuffer>>>>;

        void insertParameterToCache(const TxID& txID, SubTxID subTxID, TxParameterID paramID, const boost::optional<ByteBuffer>& blob) const;
        void deleteParametersFromCache(const TxID& txID);
        void insertAddressToCache(const WalletID& id, const boost::optional<WalletAddress>& address) const;
        void deleteAddressFromCache(const WalletID& id);
        void flushDB();
        void onModified();
        void onFlushTimer();
        void onPrepareToModify();
    private:
        friend struct sqlite::Statement;
        sqlite3* _db;
        sqlite3* m_PrivateDB;
        io::Reactor::Ptr m_Reactor;
        Key::IKdf::Ptr m_pKdf;
        io::Timer::Ptr m_FlushTimer;
        bool m_IsFlushPending;
        std::unique_ptr<sqlite::Transaction> m_DbTransaction;
        std::vector<IWalletDbObserver*> m_subscribers;

        // Wallet has ablity to track blockchain state
        // This interface allows to check and update the blockchain state 
        // in the wallet database. Used in FlyClient implementation
        struct History :public Block::SystemState::IHistory {
            bool Enum(IWalker&, const Height* pBelow) override;
            bool get_At(Block::SystemState::Full&, Height) override;
            void AddStates(const Block::SystemState::Full*, size_t nCount) override;
            void DeleteFrom(Height) override;

            IMPLEMENT_GET_PARENT_OBJ(WalletDB, m_History)
        } m_History;
        
        mutable ParameterCache m_TxParametersCache;
        mutable std::map<WalletID, boost::optional<WalletAddress>> m_AddressesCache;
    };

    namespace storage
    {
        extern const char g_szPaymentProofRequired[];

        template <typename Var>
        void setVar(IWalletDB& db, const char* name, const Var& var)
        {
            db.setVarRaw(name, &var, sizeof(var));
        }

        template <typename Var>
        bool getVar(const IWalletDB& db, const char* name, Var& var)
        {
            return db.getVarRaw(name, &var, sizeof(var));
        }

        template <typename T>
        bool getTxParameter(const IWalletDB& db, const TxID& txID, SubTxID subTxID, TxParameterID paramID, T& value)
        {
            ByteBuffer b;
            if (db.getTxParameter(txID, subTxID, paramID, b))
            {
                if (!b.empty())
                {
                    Deserializer d;
                    d.reset(b.data(), b.size());
                    d & value;
                }
                else
                {
                    ZeroObject(value);
                }
                return true;
            }
            return false;
        }

        bool getTxParameter(const IWalletDB& db, const TxID& txID, SubTxID subTxID, TxParameterID paramID, ECC::Point::Native& value);
        bool getTxParameter(const IWalletDB& db, const TxID& txID, SubTxID subTxID, TxParameterID paramID, ByteBuffer& value);
        bool getTxParameter(const IWalletDB& db, const TxID& txID, SubTxID subTxID, TxParameterID paramID, ECC::Scalar::Native& value);

        template <typename T>
        bool setTxParameter(IWalletDB& db, const TxID& txID, SubTxID subTxID, TxParameterID paramID, const T& value, bool shouldNotifyAboutChanges)
        {
            return db.setTxParameter(txID, subTxID, paramID, toByteBuffer(value), shouldNotifyAboutChanges);
        }

        bool setTxParameter(IWalletDB& db, const TxID& txID, SubTxID subTxID, TxParameterID paramID, const ECC::Point::Native& value, bool shouldNotifyAboutChanges);
        bool setTxParameter(IWalletDB& db, const TxID& txID, SubTxID subTxID, TxParameterID paramID, const ECC::Scalar::Native& value, bool shouldNotifyAboutChanges);
        bool setTxParameter(IWalletDB& db, const TxID& txID, SubTxID subTxID, TxParameterID paramID, const ByteBuffer& value, bool shouldNotifyAboutChanges);

        template <typename T>
        bool getTxParameter(const IWalletDB& db, const TxID& txID, TxParameterID paramID, T& value)
        {
            return getTxParameter(db, txID, kDefaultSubTxID, paramID, value);
        }

        bool getTxParameter(const IWalletDB& db, const TxID& txID, TxParameterID paramID, ECC::Point::Native& value);
        bool getTxParameter(const IWalletDB& db, const TxID& txID, TxParameterID paramID, ByteBuffer& value);
        bool getTxParameter(const IWalletDB& db, const TxID& txID, TxParameterID paramID, ECC::Scalar::Native& value);

        template <typename T>
        bool setTxParameter(IWalletDB& db, const TxID& txID, TxParameterID paramID, const T& value, bool shouldNotifyAboutChanges)
        {
            return setTxParameter(db, txID, kDefaultSubTxID, paramID, toByteBuffer(value), shouldNotifyAboutChanges);
        }

        bool setTxParameter(IWalletDB& db, const TxID& txID, TxParameterID paramID, const ECC::Point::Native& value, bool shouldNotifyAboutChanges);
        bool setTxParameter(IWalletDB& db, const TxID& txID, TxParameterID paramID, const ECC::Scalar::Native& value, bool shouldNotifyAboutChanges);
        bool setTxParameter(IWalletDB& db, const TxID& txID, TxParameterID paramID, const ByteBuffer& value, bool shouldNotifyAboutChanges);

        bool changeAddressExpiration(IWalletDB& walletDB, const WalletID& walletID, uint64_t expiration);
        WalletAddress createAddress(IWalletDB& walletDB);
        WalletID generateWalletIDFromIndex(IWalletDB& walletDB, uint64_t ownID);

        Coin::Status GetCoinStatus(const IWalletDB&, const Coin&, Height hTop);
        void DeduceStatus(const IWalletDB&, Coin&, Height hTop);

        // Used in statistics
        struct Totals
        {
            Amount Avail = 0;
            Amount Maturing = 0;
            Amount Incoming = 0;
            Amount Unavail = 0;
            Amount Outgoing = 0;
            Amount AvailCoinbase = 0;
            Amount Coinbase = 0;
            Amount AvailFee = 0;
            Amount Fee = 0;
            Amount Unspent = 0;

            Totals() {}
            Totals(IWalletDB& db) { Init(db); }
            void Init(IWalletDB&);
        };

        // Used for Payment Proof feature
        struct PaymentInfo
        {
            WalletID m_Sender;
            WalletID m_Receiver;

            Amount m_Amount;
            Merkle::Hash m_KernelID;
            ECC::Signature m_Signature;

            PaymentInfo();

            template <typename Archive>
            static void serializeWid(Archive& ar, WalletID& wid)
            {
                BbsChannel ch;
                wid.m_Channel.Export(ch);

                ar
                    & ch
                    & wid.m_Pk;

                wid.m_Channel = ch;
            }

            template <typename Archive>
            void serialize(Archive& ar)
            {
                serializeWid(ar, m_Sender);
                serializeWid(ar, m_Receiver);
                ar
                    & m_Amount
                    & m_KernelID
                    & m_Signature;
            }

            bool IsValid() const;
            
            std::string to_string() const;
            void Reset();
            static PaymentInfo FromByteBuffer(const ByteBuffer& data);
        };

        std::string ExportAddressesToJson(const IWalletDB& db);
        bool ImportAddressesFromJson(IWalletDB& db, const char* data, size_t size);
        std::string TxDetailsInfo(const IWalletDB::Ptr& db, const TxID& txID);
        ByteBuffer ExportPaymentProof(const IWalletDB& db, const TxID& txID);
        bool VerifyPaymentProof(const ByteBuffer& data);
    }
}
