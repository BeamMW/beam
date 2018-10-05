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
            Unconfirmed,
            Unspent,
            Locked,
            Spent,
            Draft
        };

        Coin(const ECC::Amount& amount
           , Status status = Coin::Unspent
           , const Height& createHeight = 0
           , const Height& maturity = MaxHeight
           , KeyType keyType = KeyType::Regular
           , Height confirmHeight = MaxHeight
           , Height lockedHeight = MaxHeight);
        Coin();
        bool isReward() const;
        bool isValid() const;

        uint64_t m_id;
        ECC::Amount m_amount;
        Status m_status;
        Height m_createHeight;  // For coinbase and fee coin the height of mined block, otherwise the height of last known block.
        Height m_maturity;      // coin can be spent only when chain is >= this value. Valid for confirmed coins (Unspent, Locked, Spent).
        KeyType m_key_type;
        Height m_confirmHeight;
        Merkle::Hash m_confirmHash;
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
        uint64_t  m_duration; // seconds, 0 - for single use;
        bool m_own;

        WalletAddress() : m_createTime(0), m_duration(0), m_own(false) {}
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

    struct IKeyChainObserver
    {
        
        virtual void onKeychainChanged() = 0;
        virtual void onTransactionChanged(ChangeAction action, std::vector<TxDescription>&& items) = 0;
        virtual void onSystemStateChanged() = 0;
        virtual void onTxPeerChanged() = 0;
        virtual void onAddressChanged() = 0;
    };

    struct IKeyChain
    {
        using Ptr = std::shared_ptr<IKeyChain>;
        virtual ~IKeyChain() {}


        virtual ECC::Scalar::Native calcKey(const beam::Coin& coin) const = 0;
        virtual void get_IdentityKey(ECC::Scalar::Native&) const = 0;

        virtual std::vector<beam::Coin> selectCoins(const ECC::Amount& amount, bool lock = true) = 0;
        virtual std::vector<beam::Coin> getCoinsCreatedByTx(const TxID& txId) = 0;
        virtual void store(beam::Coin& coin) = 0;
        virtual void store(std::vector<beam::Coin>& coins) = 0;
        virtual void update(const std::vector<beam::Coin>& coins) = 0;
        virtual void update(const beam::Coin& coin) = 0;
        virtual void remove(const std::vector<beam::Coin>& coins) = 0;
        virtual void remove(const beam::Coin& coin) = 0;
        virtual void clear() = 0;

        virtual void visit(std::function<bool(const beam::Coin& coin)> func) = 0;

        virtual void setVarRaw(const char* name, const void* data, size_t size) = 0;
        virtual int getVarRaw(const char* name, void* data) const = 0;
        virtual bool getBlob(const char* name, ByteBuffer& var) const = 0;
        virtual Height getCurrentHeight() const = 0;
        virtual uint64_t getKnownStateCount() const = 0;
        virtual Block::SystemState::ID getKnownStateID(Height height) = 0;
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

        template <typename Var>
        void setVar(const char* name, const Var& var)
        {
            setVarRaw(name, &var, sizeof(var));
        }

        template <typename Var>
        bool getVar(const char* name, Var& var) const
        {
            return getVarRaw(name, &var) == sizeof(var);
        }

        virtual Timestamp getLastUpdateTime() const = 0;
        virtual void setSystemStateID(const Block::SystemState::ID& stateID) = 0;
        virtual bool getSystemStateID(Block::SystemState::ID& stateID) const = 0;

        virtual void subscribe(IKeyChainObserver* observer) = 0;
        virtual void unsubscribe(IKeyChainObserver* observer) = 0;

        virtual void changePassword(const SecString& password) = 0;

        virtual bool setTxParameter(const TxID& txID, wallet::TxParameterID paramID, const ByteBuffer& blob) = 0;
        virtual bool getTxParameter(const TxID& txID, wallet::TxParameterID paramID, ByteBuffer& blob) = 0;
    };

    class Keychain : public IKeyChain, public std::enable_shared_from_this<Keychain>
    {
    public:
        static bool isInitialized(const std::string& path);
        static Ptr init(const std::string& path, const SecString& password, const ECC::NoLeak<ECC::uintBig>& secretKey);
        static Ptr open(const std::string& path, const SecString& password);

        Keychain(const ECC::NoLeak<ECC::uintBig>& secretKey );
        ~Keychain();

        ECC::Scalar::Native calcKey(const beam::Coin& coin) const override;
        void get_IdentityKey(ECC::Scalar::Native&) const override;
        std::vector<beam::Coin> selectCoins(const ECC::Amount& amount, bool lock = true) override;
        std::vector<beam::Coin> getCoinsCreatedByTx(const TxID& txId) override;
        void store(beam::Coin& coin) override;
        void store(std::vector<beam::Coin>& coins) override;
        void update(const std::vector<beam::Coin>& coins) override;
        void update(const beam::Coin& coin) override;
        void remove(const std::vector<beam::Coin>& coins) override;
        void remove(const beam::Coin& coin) override;
        void clear() override;

        void visit(std::function<bool(const beam::Coin& coin)> func) override;

        void setVarRaw(const char* name, const void* data, size_t size) override;
        int getVarRaw(const char* name, void* data) const override;
        bool getBlob(const char* name, ByteBuffer& var) const override;
        Height getCurrentHeight() const override;
        uint64_t getKnownStateCount() const override;
        Block::SystemState::ID getKnownStateID(Height height) override;
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

        void subscribe(IKeyChainObserver* observer) override;
        void unsubscribe(IKeyChainObserver* observer) override;

        void changePassword(const SecString& password) override;

        bool setTxParameter(const TxID& txID, wallet::TxParameterID paramID, const ByteBuffer& blob) override;
        bool getTxParameter(const TxID& txID, wallet::TxParameterID paramID, ByteBuffer& blob) override;
    private:
        void storeImpl(Coin& coin);
        void notifyKeychainChanged();
        void notifyTransactionChanged(ChangeAction action, std::vector<TxDescription>&& items);
        void notifySystemStateChanged();
        void notifyAddressChanged();
    private:

        sqlite3* _db;
        ECC::Kdf m_kdf;

        std::vector<IKeyChainObserver*> m_subscribers;
    };

    namespace wallet
    {
        template <typename T>
        ByteBuffer toByteBuffer(const T& value)
        {
            Serializer s;
            s & value;
            ByteBuffer b;
            s.swap_buf(b);
            return b;
        }

        ByteBuffer toByteBuffer(const ECC::Point::Native& value);
        ByteBuffer toByteBuffer(const ECC::Scalar::Native& value);

        template <typename T>
        bool getTxParameter(IKeyChain::Ptr db, const TxID& txID, TxParameterID paramID, T& value)
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

        bool getTxParameter(IKeyChain::Ptr db, const TxID& txID, TxParameterID paramID, ECC::Point::Native& value);
        bool getTxParameter(IKeyChain::Ptr db, const TxID& txID, TxParameterID paramID, ECC::Scalar::Native& value);
        bool getTxParameter(IKeyChain::Ptr db, const TxID& txID, TxParameterID paramID, ByteBuffer& value);

        template <typename T>
        bool setTxParameter(IKeyChain::Ptr db, const TxID& txID, TxParameterID paramID, const T& value)
        {
            return db->setTxParameter(txID, paramID, toByteBuffer(value));
        }

        bool setTxParameter(IKeyChain::Ptr db, const TxID& txID, TxParameterID paramID, const ECC::Point::Native& value);
        bool setTxParameter(IKeyChain::Ptr db, const TxID& txID, TxParameterID paramID, const ECC::Scalar::Native& value);
        bool setTxParameter(IKeyChain::Ptr db, const TxID& txID, TxParameterID paramID, const ByteBuffer& value);

        Amount getAvailable(beam::IKeyChain::Ptr keychain);
        Amount getAvailableByType(beam::IKeyChain::Ptr keychain, Coin::Status status, KeyType keyType);
        Amount getTotal(beam::IKeyChain::Ptr keychain, Coin::Status status);
        Amount getTotalByType(beam::IKeyChain::Ptr keychain, Coin::Status status, KeyType keyType);
    }
}
