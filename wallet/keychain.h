#pragma once

#include "core/common.h"
#include "core/ecc_native.h"

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
        Coin(uint64_t id, ECC::Amount amount);

		uint64_t m_id;
        ECC::Amount m_amount;
        Status m_status;
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
    };
}
