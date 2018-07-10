#pragma once

#include <boost/optional.hpp>
#include "core/common.h"
#include "core/ecc_native.h"
#include "wallet/common.h"

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
            Spent
        };

        Coin(const ECC::Amount& amount
           , Status status = Coin::Unspent
           , const Height& createHeight = 0
           , const Height& maturity = MaxHeight
           , KeyType keyType = KeyType::Regular
           , Height confirmHeight = MaxHeight
           , Height lockedHeight = MaxHeight);
        Coin();

        uint64_t m_id;
        ECC::Amount m_amount;
        Status m_status;
        Height m_createHeight;  // For coinbase and fee coin the height of mined block, otherwise the height of last known block.
        Height m_maturity;      // coin can be spent only when chain is >= this value. Valid for confirmed coins (Unspent, Locked, Spent).
        KeyType m_key_type;
        Height m_confirmHeight; 
        Merkle::Hash m_confirmHash;
        Height m_lockedHeight;
        boost::optional<Uuid> m_createTxId;
        boost::optional<Uuid> m_spentTxId;
    };

	struct IKeyChainObserver
	{
		virtual void onKeychainChanged() = 0;
		virtual void onTransactionChanged() = 0;
	};

    struct IKeyChain
    {
        using Ptr = std::shared_ptr<IKeyChain>;
        virtual ~IKeyChain() {}


        virtual ECC::Scalar::Native calcKey(const beam::Coin& coin) const = 0;

        virtual std::vector<beam::Coin> getCoins(const ECC::Amount& amount, bool lock = true) = 0;
        virtual void store(beam::Coin& coin) = 0;
        virtual void store(std::vector<beam::Coin>& coins) = 0;
        virtual void update(const std::vector<beam::Coin>& coins) = 0;
        virtual void update(const beam::Coin& coin) = 0;
        virtual void remove(const std::vector<beam::Coin>& coins) = 0;
        virtual void remove(const beam::Coin& coin) = 0;

		virtual void visit(std::function<bool(const beam::Coin& coin)> func) = 0;

		virtual void setVarRaw(const char* name, const void* data, int size) = 0;
		virtual int getVarRaw(const char* name, void* data) const = 0;
        virtual Height getCurrentHeight() const = 0;
        virtual Block::SystemState::ID getKnownStateID(Height height) = 0;
        virtual void rollbackConfirmedUtxo(Height minHeight) = 0;

        virtual std::vector<TxDescription> getTxHistory(uint64_t start = 0, int count = std::numeric_limits<int>::max()) = 0;
        virtual boost::optional<TxDescription> getTx(const Uuid& txId) = 0;
        virtual void saveTx(const TxDescription& p) = 0;
        virtual void deleteTx(const Uuid& txId) = 0;

        // Rolls back coin changes in db concerning given tx
        virtual void rollbackTx(const Uuid& txId) = 0;

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
		virtual void setSystemStateID(const Block::SystemState::ID& stateID) = 0;
		virtual bool getSystemStateID(Block::SystemState::ID& stateID) const = 0;

		virtual void subscribe(IKeyChainObserver* observer) = 0;
		virtual void unsubscribe(IKeyChainObserver* observer) = 0;
	};

    struct Keychain : IKeyChain
    {
        static bool isInitialized(const std::string& path);
        static Ptr init(const std::string& path, const std::string& password, const ECC::NoLeak<ECC::uintBig>& secretKey);
        static Ptr open(const std::string& path, const std::string& password);

        Keychain(const ECC::NoLeak<ECC::uintBig>& secretKey );
        ~Keychain();

        ECC::Scalar::Native calcKey(const beam::Coin& coin) const override;
        std::vector<beam::Coin> getCoins(const ECC::Amount& amount, bool lock = true) override;
        void store(beam::Coin& coin) override;
        void store(std::vector<beam::Coin>& coins) override;
        void update(const std::vector<beam::Coin>& coins) override;
        void update(const beam::Coin& coin) override;
        void remove(const std::vector<beam::Coin>& coins) override;
        void remove(const beam::Coin& coin) override;
		void visit(std::function<bool(const beam::Coin& coin)> func) override;

		void setVarRaw(const char* name, const void* data, int size) override;
		int getVarRaw(const char* name, void* data) const override;
        Height getCurrentHeight() const override;
        Block::SystemState::ID getKnownStateID(Height height) override;
        void rollbackConfirmedUtxo(Height minHeight) override;

        std::vector<TxDescription> getTxHistory(uint64_t start, int count) override;
        boost::optional<TxDescription> getTx(const Uuid& txId) override;
        void saveTx(const TxDescription& p) override;
        void deleteTx(const Uuid& txId) override;
        void rollbackTx(const Uuid& txId) override;

		void setSystemStateID(const Block::SystemState::ID& stateID) override;
		bool getSystemStateID(Block::SystemState::ID& stateID) const override;

		void subscribe(IKeyChainObserver* observer) override;
		void unsubscribe(IKeyChainObserver* observer) override;
    private:
        void storeImpl(Coin& coin);
		void notifyKeychainChanged();
		void notifyTransactionChanged();
    private:

        sqlite3* _db;
        ECC::Kdf m_kdf;

		std::vector<IKeyChainObserver*> m_subscribers;
    };

	namespace wallet
	{
		Amount getAvailable(beam::IKeyChain::Ptr keychain);
		Amount getAvailableByType(beam::IKeyChain::Ptr keychain, Coin::Status status, KeyType keyType);
		Amount getTotal(beam::IKeyChain::Ptr keychain, Coin::Status status);
		Amount getTotalByType(beam::IKeyChain::Ptr keychain, Coin::Status status, KeyType keyType);
	}
}
