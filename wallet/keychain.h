#pragma once

#include "core/common.h"
#include "core/ecc_native.h"

namespace beam
{
    struct Coin
    {
        Coin();
        Coin(const ECC::Scalar& key, ECC::Amount amount);
        ECC::Scalar m_key;
        ECC::Amount m_amount;
    };

    struct IKeyChain
    {
        using Ptr = std::shared_ptr<IKeyChain>;
        virtual ~IKeyChain() {}
        virtual ECC::Scalar getNextKey() = 0;
        virtual std::vector<beam::Coin> getCoins(const ECC::Amount& amount) = 0;
        virtual void update(const std::vector<beam::Coin>& coins) = 0;
        virtual void remove(const std::vector<beam::Coin>& coins) = 0;
    };
}
