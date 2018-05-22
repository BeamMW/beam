#pragma once

#include "core/common.h"
#include "core/ecc_native.h"

struct sqlite3;
struct Nonce;

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

        Coin();
        Coin(uint64_t id, const ECC::Amount& amount, Status status = Coin::Unspent, const Height& height = 0, bool isCoinbase = false);

        uint64_t m_id;
        ECC::Amount m_amount;
        Status m_status;
        Height m_height;
        bool m_isCoinbase;
    };

    struct IKeyChain
    {
        using Ptr = std::shared_ptr<IKeyChain>;
        virtual ~IKeyChain() {}

        virtual uint64_t getNextID() = 0;

        // TODO: change it to native
        virtual ECC::Scalar calcKey(uint64_t id) = 0;

        virtual std::vector<beam::Coin> getCoins(const ECC::Amount& amount, bool lock = true) = 0;
        virtual void store(const beam::Coin& coin) = 0;
        virtual void update(const std::vector<beam::Coin>& coins) = 0;
        virtual void remove(const std::vector<beam::Coin>& coins) = 0;

		virtual void visit(std::function<bool(const beam::Coin& coin)> func) = 0;
    };

    struct Keychain : IKeyChain
    {
        static Ptr init(const std::string& password);
        static Ptr open(const std::string& password);
        static const char* getName();

        Keychain(const std::string& pass);
        ~Keychain();

        uint64_t getNextID() override;
        ECC::Scalar calcKey(uint64_t id) override;
        std::vector<beam::Coin> getCoins(const ECC::Amount& amount, bool lock = true) override;
        void store(const beam::Coin& coin) override;
        void update(const std::vector<beam::Coin>& coins) override;
        void remove(const std::vector<beam::Coin>& coins) override;
		void visit(std::function<bool(const beam::Coin& coin)> func) override;

    private:

        sqlite3* _db;
        std::shared_ptr<Nonce> _nonce;
    };
}
