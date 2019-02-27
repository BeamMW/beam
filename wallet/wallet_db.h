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

namespace beam
{
    const uint32_t EmptyCoinSession = 0;

    struct Coin
    {
        enum Status
        {
            Unavailable,
            Available,
            Maturing,
            Outgoing,
            Incoming,
            ChangeV0, // deprecated.
            Spent,

			count
        };

        Coin(Amount amount = 0, Key::Type keyType = Key::Type::Regular);
		bool operator==(const Coin&) const;
		bool operator!=(const Coin&) const;
		bool isReward() const;
        std::string toStringID() const;
        Amount getAmount() const;

        typedef Key::IDV ID;
        ID m_ID;

        Status m_status;
        Height m_maturity;      // coin can be spent only when chain is >= this value. Valid for confirmed coins (Available, Outgoing, Incoming, Change, Spent, Maturing).
        Height m_confirmHeight;
		Height m_spentHeight;
		boost::optional<TxID> m_createTxId;
        boost::optional<TxID> m_spentTxId;
        uint32_t m_sessionId;

		bool IsMaturityValid() const; // is/was the UTXO confirmed?
		Height get_Maturity() const; // would return MaxHeight unless the UTXO was confirmed
        static boost::optional<Coin::ID> FromString(const std::string& str);
    };

    using CoinIDList = std::vector<Coin::ID>;

    struct WalletAddress
    {
        WalletID m_walletID;
        std::string m_label;
        std::string m_category;
        Timestamp m_createTime;
        uint64_t  m_duration; // if it equals 0 then address never expires
        uint64_t  m_OwnID; // set for own address

        bool isExpired() const
        {
            return getTimestamp() > getExpirationTime();
        }

        Timestamp getCreateTime() const
        {
            return m_createTime;
        }

        Timestamp getExpirationTime() const
        {
            if (m_duration == 0)
            {
                return Timestamp(-1);
            } 
            return m_createTime + m_duration;
        }

        WalletAddress() 
            : m_walletID(Zero)
            , m_createTime(0)
            , m_duration(24 * 60 * 60) // 24h
            , m_OwnID(false)
        {}
    };

    struct TxParameter
    {
        TxID m_txID;
        int m_paramID;
        ByteBuffer m_value;
    };

    enum class ChangeAction
    {
        Added,
        Removed,
        Updated,
        Reset
    };

    struct IWalletDbObserver
    {
        virtual void onCoinsChanged() = 0;
        virtual void onTransactionChanged(ChangeAction action, std::vector<TxDescription>&& items) = 0;
        virtual void onSystemStateChanged() = 0;
        virtual void onAddressChanged() = 0;
    };

    struct IWalletDB
    {
        using Ptr = std::shared_ptr<IWalletDB>;
        virtual ~IWalletDB() {}

        virtual beam::Key::IKdf::Ptr get_MasterKdf() const = 0;
        beam::Key::IKdf::Ptr get_ChildKdf(Key::Index) const;
        void calcCommitment(ECC::Scalar::Native& sk, ECC::Point& comm, const Coin::ID&);
        virtual uint64_t AllocateKidRange(uint64_t nCount) = 0;
        virtual std::vector<Coin> selectCoins(Amount amount) = 0;
        virtual std::vector<Coin> getCoinsCreatedByTx(const TxID& txId) = 0;
        virtual std::vector<Coin> getCoinsByID(const CoinIDList& ids) = 0;
        virtual void store(Coin& coin) = 0;
        virtual void store(std::vector<Coin>&) = 0;
        virtual void save(const Coin& coin) = 0;
        virtual void save(const std::vector<Coin>& coins) = 0;
        virtual void remove(const Coin::ID&) = 0;
        virtual void remove(const std::vector<Coin::ID>&) = 0;
        virtual bool find(Coin& coin) = 0;
        virtual void clear() = 0;

        virtual void visit(std::function<bool(const Coin& coin)> func) = 0;

        virtual void setVarRaw(const char* name, const void* data, size_t size) = 0;
        virtual bool getVarRaw(const char* name, void* data, int size) const = 0;
        virtual bool getBlob(const char* name, ByteBuffer& var) const = 0;
        virtual Height getCurrentHeight() const = 0;
        virtual void rollbackConfirmedUtxo(Height minHeight) = 0;

        virtual std::vector<TxDescription> getTxHistory(uint64_t start = 0, int count = std::numeric_limits<int>::max()) = 0;
        virtual boost::optional<TxDescription> getTx(const TxID& txId) = 0;
        virtual void saveTx(const TxDescription& p) = 0;
        virtual void deleteTx(const TxID& txId) = 0;

        // Rolls back coin changes in db concerning given tx
        virtual void rollbackTx(const TxID& txId) = 0;

        virtual std::vector<WalletAddress> getAddresses(bool own) const = 0;
        virtual void saveAddress(const WalletAddress&) = 0;
        virtual void setExpirationForAllAddresses(uint64_t expiration) = 0;
        virtual boost::optional<WalletAddress> getAddress(const WalletID&) = 0;
        virtual void deleteAddress(const WalletID&) = 0;

        virtual Timestamp getLastUpdateTime() const = 0;
        virtual void setSystemStateID(const Block::SystemState::ID& stateID) = 0;
        virtual bool getSystemStateID(Block::SystemState::ID& stateID) const = 0;

        virtual void subscribe(IWalletDbObserver* observer) = 0;
        virtual void unsubscribe(IWalletDbObserver* observer) = 0;

        virtual void changePassword(const SecString& password) = 0;

        virtual bool setTxParameter(const TxID& txID, wallet::TxParameterID paramID,
            const ByteBuffer& blob, bool shouldNotifyAboutChanges) = 0;
        virtual bool getTxParameter(const TxID& txID, wallet::TxParameterID paramID, ByteBuffer& blob) const = 0;

        virtual Block::SystemState::IHistory& get_History() = 0;
        virtual void ShrinkHistory() = 0;

        virtual Amount getTransferredByTx(TxStatus status, bool isSender) const = 0;
    };

    class WalletDB : public IWalletDB
    {
        WalletDB();
    public:
        static bool isInitialized(const std::string& path);
        static Ptr init(const std::string& path, const SecString& password, const ECC::NoLeak<ECC::uintBig>& secretKey);
        static Ptr open(const std::string& path, const SecString& password);

        WalletDB(const ECC::NoLeak<ECC::uintBig>& secretKey);
        ~WalletDB();

        beam::Key::IKdf::Ptr get_MasterKdf() const override;
        uint64_t AllocateKidRange(uint64_t nCount) override;
        std::vector<Coin> selectCoins(Amount amount) override;
        std::vector<Coin> getCoinsCreatedByTx(const TxID& txId) override;
        std::vector<Coin> getCoinsByID(const CoinIDList& ids) override;
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
        bool getBlob(const char* name, ByteBuffer& var) const override;
        Height getCurrentHeight() const override;
        void rollbackConfirmedUtxo(Height minHeight) override;

        std::vector<TxDescription> getTxHistory(uint64_t start, int count) override;
        boost::optional<TxDescription> getTx(const TxID& txId) override;
        void saveTx(const TxDescription& p) override;
        void deleteTx(const TxID& txId) override;
        void rollbackTx(const TxID& txId) override;

        std::vector<WalletAddress> getAddresses(bool own) const override;
        void saveAddress(const WalletAddress&) override;
        void setExpirationForAllAddresses(uint64_t expiration) override;
        boost::optional<WalletAddress> getAddress(const WalletID&) override;
        void deleteAddress(const WalletID&) override;

        Timestamp getLastUpdateTime() const override;
        void setSystemStateID(const Block::SystemState::ID& stateID) override;
        bool getSystemStateID(Block::SystemState::ID& stateID) const override;

        void subscribe(IWalletDbObserver* observer) override;
        void unsubscribe(IWalletDbObserver* observer) override;

        void changePassword(const SecString& password) override;

        bool setTxParameter(const TxID& txID, wallet::TxParameterID paramID,
            const ByteBuffer& blob, bool shouldNotifyAboutChanges) override;
        bool getTxParameter(const TxID& txID, wallet::TxParameterID paramID, ByteBuffer& blob) const override;

        Block::SystemState::IHistory& get_History() override;
        void ShrinkHistory() override;

        Amount getTransferredByTx(TxStatus status, bool isSender) const override;

    private:
        void removeImpl(const Coin::ID& cid);
        void notifyCoinsChanged();
        void notifyTransactionChanged(ChangeAction action, std::vector<TxDescription>&& items);
        void notifySystemStateChanged();
        void notifyAddressChanged();
		void CreateStorageTable();
		static uint64_t get_RandomID();
		bool updateRaw(const Coin&);
		void insertRaw(const Coin&);
		void insertNew(Coin&);
		void saveRaw(const Coin&);

        using ParameterCache = std::map<TxID, std::map<wallet::TxParameterID, boost::optional<ByteBuffer>>>;

        void insertParameterToCache(const TxID& txID, wallet::TxParameterID paramID, const boost::optional<ByteBuffer>& blob) const;
        void deleteParametersFromCache(const TxID& txID);
        void insertAddressToCache(const WalletID& id, const boost::optional<WalletAddress>& address) const;
        void deleteAddressFromCache(const WalletID& id);
	private:

        sqlite3* _db;
        Key::IKdf::Ptr m_pKdf;

        std::vector<IWalletDbObserver*> m_subscribers;

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

    namespace wallet
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
        bool getTxParameter(const IWalletDB& db, const TxID& txID, TxParameterID paramID, T& value)
        {
            ByteBuffer b;
            if (db.getTxParameter(txID, paramID, b))
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

        bool getTxParameter(const IWalletDB& db, const TxID& txID, TxParameterID paramID, ECC::Point::Native& value);
        bool getTxParameter(const IWalletDB& db, const TxID& txID, TxParameterID paramID, ByteBuffer& value);
        bool getTxParameter(const IWalletDB& db, const TxID& txID, TxParameterID paramID, ECC::Scalar::Native& value);

        template <typename T>
        bool setTxParameter(IWalletDB& db, const TxID& txID, TxParameterID paramID, const T& value, bool shouldNotifyAboutChanges)
        {
            return db.setTxParameter(txID, paramID, toByteBuffer(value), shouldNotifyAboutChanges);
        }

        bool setTxParameter(IWalletDB& db, const TxID& txID, TxParameterID paramID, const ECC::Point::Native& value, bool shouldNotifyAboutChanges);
        bool setTxParameter(IWalletDB& db, const TxID& txID, TxParameterID paramID, const ECC::Scalar::Native& value, bool shouldNotifyAboutChanges);
        bool setTxParameter(IWalletDB& db, const TxID& txID, TxParameterID paramID, const ByteBuffer& value, bool shouldNotifyAboutChanges);

        bool changeAddressExpiration(IWalletDB& walletDB, const WalletID& walletID, uint64_t expiration);
        WalletAddress createAddress(IWalletDB& walletDB);
        WalletID generateWalletIDFromIndex(IWalletDB& walletDB, uint64_t ownID);
        Amount getSpentByTx(const IWalletDB& walletDB, TxStatus status);
        Amount getReceivedByTx(const IWalletDB& walletDB, TxStatus status);

		Coin::Status GetCoinStatus(const IWalletDB&, const Coin&, Height hTop);
		void DeduceStatus(const IWalletDB&, Coin&, Height hTop);

		struct Totals
		{
			Amount Avail;
			Amount Maturing;
			Amount Incoming;
			Amount Unavail;
			Amount Outgoing;
			Amount AvailCoinbase;
			Amount Coinbase;
			Amount AvailFee;
			Amount Fee;
			Amount Unspent;

			Totals() {}
			Totals(IWalletDB& db) { Init(db); }
			void Init(IWalletDB&);
		};

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
        ByteBuffer ExportPaymentProof(const IWalletDB& db, const TxID& txID);
        bool VerifyPaymentProof(const ByteBuffer& data);
    }
}
