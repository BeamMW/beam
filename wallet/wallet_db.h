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

#include <boost/optional.hpp>
#include "core/common.h"
#include "core/ecc_native.h"
#include "wallet/common.h"
#include "utility/io/address.h"
#include "secstring.h"

struct sqlite3;

namespace beam
{
    struct Coin
    {
        enum Status
        {
            Unavailable,
            Available,
            Maturing,
            Outgoing,
            Incoming,
            Change,
            Spent
        };

        Coin(Amount amount
           , Status status = Coin::Maturing
           , Height maturity = MaxHeight
           , Key::Type keyType = Key::Type::Regular
           , Height confirmHeight = MaxHeight
           , Height lockedHeight = MaxHeight);
        Coin();
        bool isReward() const;
        bool isValid() const;

		typedef Key::IDVC ID;
		ID m_ID;

        Status m_status;
        Height m_createHeight;  // For coinbase and fee coin the height of mined block, otherwise the height of last known block.
        Height m_maturity;      // coin can be spent only when chain is >= this value. Valid for confirmed coins (Available, Outgoing, Incoming, Change, Spent, Maturing).
        Height m_confirmHeight;
        Height m_lockedHeight;
        boost::optional<TxID> m_createTxId;
        boost::optional<TxID> m_spentTxId;
    };

    struct TxPeer
    {
        WalletID m_walletID;
        std::string m_label;
        std::string m_address;
    };

    struct WalletAddress
    {
        WalletID m_walletID;
        std::string m_label;
        std::string m_category;
        Timestamp m_createTime;
        uint64_t  m_duration;
		uint64_t  m_OwnID; // set for own address

        bool isExpired() const
        {
            return getTimestamp() > (m_createTime + m_duration);
        }

        WalletAddress() 
            : m_createTime(0)
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
        virtual void onTxPeerChanged() = 0;
        virtual void onAddressChanged() = 0;
    };

    struct IWalletDB
    {
        using Ptr = std::shared_ptr<IWalletDB>;
        virtual ~IWalletDB() {}

		virtual beam::Key::IKdf::Ptr get_MasterKdf() const = 0;
		virtual beam::Key::IKdf::Ptr get_ChildKdf(Key::Index) const;
		virtual ECC::Scalar::Native calcKey(const Coin::ID&) const = 0;
		virtual uint64_t AllocateKidRange(uint64_t nCount) = 0;
        virtual std::vector<Coin> selectCoins(const Amount& amount, bool lock = true) = 0;
        virtual std::vector<Coin> getCoinsCreatedByTx(const TxID& txId) = 0;
		virtual void store(Coin& coin) = 0;
		virtual void store(std::vector<Coin>&) = 0;
		virtual void save(const Coin& coin) = 0;
        virtual void save(const std::vector<Coin>& coins) = 0;
        virtual void remove(const Coin::ID&) = 0;
		virtual void remove(const std::vector<Coin::ID>&) = 0;
		virtual bool find(Coin& coin) = 0;
		virtual void clear() = 0;
		virtual void maturingCoins() = 0;

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

        virtual std::vector<TxPeer> getPeers() = 0;
        virtual void addPeer(const TxPeer&) = 0;
        virtual boost::optional<TxPeer> getPeer(const WalletID&) = 0;
        virtual void clearPeers() = 0;

        virtual std::vector<WalletAddress> getAddresses(bool own) = 0;
        virtual void saveAddress(const WalletAddress&) = 0;
        virtual boost::optional<WalletAddress> getAddress(const WalletID&) = 0;
        virtual void deleteAddress(const WalletID&) = 0;

		void createAddress(WalletAddress&);
		void createAndSaveAddress(WalletAddress&);
		Timestamp GetLastChannel(BbsChannel&);
		void SetLastChannel(BbsChannel);

        template <typename Var>
        void setVar(const char* name, const Var& var)
        {
            setVarRaw(name, &var, sizeof(var));
        }

        template <typename Var>
        bool getVar(const char* name, Var& var) const
        {
            return getVarRaw(name, &var, sizeof(var));
        }

        virtual Timestamp getLastUpdateTime() const = 0;
        virtual void setSystemStateID(const Block::SystemState::ID& stateID) = 0;
        virtual bool getSystemStateID(Block::SystemState::ID& stateID) const = 0;

        virtual void subscribe(IWalletDbObserver* observer) = 0;
        virtual void unsubscribe(IWalletDbObserver* observer) = 0;

        virtual void changePassword(const SecString& password) = 0;

        virtual bool setTxParameter(const TxID& txID, wallet::TxParameterID paramID, const ByteBuffer& blob) = 0;
        virtual bool getTxParameter(const TxID& txID, wallet::TxParameterID paramID, ByteBuffer& blob) = 0;

		virtual Block::SystemState::IHistory& get_History() = 0;
		virtual void ShrinkHistory() = 0;
	};

    class WalletDB : public IWalletDB, public std::enable_shared_from_this<WalletDB>
    {
		WalletDB();
	public:
        static bool isInitialized(const std::string& path);
        static Ptr init(const std::string& path, const SecString& password, const ECC::NoLeak<ECC::uintBig>& secretKey);
        static Ptr open(const std::string& path, const SecString& password);

        WalletDB(const ECC::NoLeak<ECC::uintBig>& secretKey);
        ~WalletDB();

		beam::Key::IKdf::Ptr get_MasterKdf() const override;
		ECC::Scalar::Native calcKey(const Coin::ID&) const override;
		uint64_t AllocateKidRange(uint64_t nCount) override;
		std::vector<Coin> selectCoins(const Amount& amount, bool lock = true) override;
        std::vector<Coin> getCoinsCreatedByTx(const TxID& txId) override;
		void store(Coin& coin) override;
		void store(std::vector<Coin>&) override;
		void save(const Coin& coin) override;
		void save(const std::vector<Coin>& coins) override;
		void remove(const Coin::ID&) override;
		void remove(const std::vector<Coin::ID>&) override;
		bool find(Coin& coin) override;
		void clear() override;
		void maturingCoins() override;

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

        std::vector<TxPeer> getPeers() override;
        void addPeer(const TxPeer&) override;
        boost::optional<TxPeer> getPeer(const WalletID&) override;
        void clearPeers() override;

        std::vector<WalletAddress> getAddresses(bool own) override;
        void saveAddress(const WalletAddress&) override;
        boost::optional<WalletAddress> getAddress(const WalletID&) override;
        void deleteAddress(const WalletID&) override;

        Timestamp getLastUpdateTime() const override;
        void setSystemStateID(const Block::SystemState::ID& stateID) override;
        bool getSystemStateID(Block::SystemState::ID& stateID) const override;

        void subscribe(IWalletDbObserver* observer) override;
        void unsubscribe(IWalletDbObserver* observer) override;

        void changePassword(const SecString& password) override;

        bool setTxParameter(const TxID& txID, wallet::TxParameterID paramID, const ByteBuffer& blob) override;
        bool getTxParameter(const TxID& txID, wallet::TxParameterID paramID, ByteBuffer& blob) override;

		Block::SystemState::IHistory& get_History() override;
		void ShrinkHistory() override;

    private:
        void storeImpl(const Coin& coin);
		void removeImpl(const Coin::ID& cid);
		void notifyCoinsChanged();
        void notifyTransactionChanged(ChangeAction action, std::vector<TxDescription>&& items);
        void notifySystemStateChanged();
        void notifyAddressChanged();
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
    };

    namespace wallet
    {
        template <typename T>
        bool getTxParameter(IWalletDB::Ptr db, const TxID& txID, TxParameterID paramID, T& value)
        {
            ByteBuffer b;
            if (db->getTxParameter(txID, paramID, b))
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

        bool getTxParameter(IWalletDB::Ptr db, const TxID& txID, TxParameterID paramID, ECC::Point::Native& value);
        bool getTxParameter(IWalletDB::Ptr db, const TxID& txID, TxParameterID paramID, ECC::Scalar::Native& value);
        bool getTxParameter(IWalletDB::Ptr db, const TxID& txID, TxParameterID paramID, ByteBuffer& value);

        template <typename T>
        bool setTxParameter(IWalletDB::Ptr db, const TxID& txID, TxParameterID paramID, const T& value)
        {
            return db->setTxParameter(txID, paramID, toByteBuffer(value));
        }

        bool setTxParameter(IWalletDB::Ptr db, const TxID& txID, TxParameterID paramID, const ECC::Point::Native& value);
        bool setTxParameter(IWalletDB::Ptr db, const TxID& txID, TxParameterID paramID, const ECC::Scalar::Native& value);
        bool setTxParameter(IWalletDB::Ptr db, const TxID& txID, TxParameterID paramID, const ByteBuffer& value);

        Amount getAvailable(beam::IWalletDB::Ptr walletDB);
        Amount getAvailableByType(beam::IWalletDB::Ptr walletDB, Coin::Status status, Key::Type keyType);
        Amount getTotal(beam::IWalletDB::Ptr walletDB, Coin::Status status);
        Amount getTotalByType(beam::IWalletDB::Ptr walletDB, Coin::Status status, Key::Type keyType);
    }
}
