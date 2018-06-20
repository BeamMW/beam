#pragma once

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

        Coin(const ECC::Amount& amount, Status status = Coin::Unspent, const Height& height = 0, const Height& maturity = MaxHeight, KeyType keyType = KeyType::Regular);
        Coin();

        uint64_t m_id;
        Height m_height; // For coinbase and fee coin the height of mined block, otherwise the height of last known block.
        Height m_maturity; // coin can be spent only when chain is >= this value. Valid for confirmed coins (Unspent, Locked, Spent).
        KeyType m_key_type;
        ECC::Amount m_amount;

        Status m_status;
    };

    struct HistoryRecord
    {
        enum TxType
        {};
        enum Status
        {
            Pending,
            InProgress,
            Cancelled,
            Completed,
            Failed
        };

        HistoryRecord(const Uuid& id, Amount amount, uint64_t peerId, const ByteBuffer& message)
            : m_txId{id}
            , m_amount{ 0 }
            , m_peerId{ peerId }
            , m_message{message}
            , m_createTime{ Timestamp(-1) }
            , m_modifyTime{ Timestamp(-1) }
            , m_sender{ false }
            , m_status{ Pending }
        {}
        HistoryRecord() : HistoryRecord(Uuid{ 0 }, 0, 0, {})
        {
        }
        Uuid m_txId;
        Amount m_amount;
        uint64_t m_peerId;
        ByteBuffer m_message;
        Timestamp m_createTime;
        Timestamp m_modifyTime;
        bool m_sender;
        Status m_status;
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
        virtual void remove(const std::vector<beam::Coin>& coins) = 0;
        virtual void remove(const beam::Coin& coin) = 0;

		virtual void visit(std::function<bool(const beam::Coin& coin)> func) = 0;

		virtual void setVarRaw(const char* name, const void* data, int size) = 0;
		virtual int getVarRaw(const char* name, void* data) const = 0;
        virtual Height getCurrentHeight() const = 0;

        virtual std::vector<TxDescription> getTxHistory(uint64_t start, size_t count) = 0;
        virtual boost::optional<TxDescription> getTx(const Uuid& txId) = 0;
        virtual void saveTx(const TxDescription& p) = 0;
        virtual void deleteTx(const Uuid& txId) = 0;

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
	};

    struct Keychain : IKeyChain
    {
        static bool isInitialized();
        static Ptr init(const std::string& password, const ECC::NoLeak<ECC::uintBig>& secretKey);
        static Ptr open(const std::string& password);
        static const char* getName();

        Keychain(const ECC::NoLeak<ECC::uintBig>& secretKey );
        ~Keychain();

        ECC::Scalar::Native calcKey(const beam::Coin& coin) const override;
        std::vector<beam::Coin> getCoins(const ECC::Amount& amount, bool lock = true) override;
        void store(beam::Coin& coin) override;
        void store(std::vector<beam::Coin>& coins) override;
        void update(const std::vector<beam::Coin>& coins) override;
        void remove(const std::vector<beam::Coin>& coins) override;
        void remove(const beam::Coin& coin) override;
		void visit(std::function<bool(const beam::Coin& coin)> func) override;

		void setVarRaw(const char* name, const void* data, int size) override;
		int getVarRaw(const char* name, void* data) const override;
        Height getCurrentHeight() const override;

        std::vector<TxDescription> getTxHistory(uint64_t start, size_t count) override;
        boost::optional<TxDescription> getTx(const Uuid& txId) override;
        void saveTx(const TxDescription& p) override;
        void deleteTx(const Uuid& txId) override;

		void setSystemStateID(const Block::SystemState::ID& stateID) override;
		bool getSystemStateID(Block::SystemState::ID& stateID) const override;
    private:
        void storeImpl(Coin& coin);
    private:

        sqlite3* _db;
        ECC::Kdf m_kdf;
    };
}
