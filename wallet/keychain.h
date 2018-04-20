#pragma once

#include "core/common.h"
#include "core/ecc_native.h"

namespace beam
{
    struct Coin
    {
        Coin(const ECC::Scalar& key, ECC::Amount amount);
        ECC::Scalar m_key;
        ECC::Amount m_amount;
    };

    struct IKeyChain
    {
        using Ptr = std::shared_ptr<IKeyChain>;
        virtual std::vector<beam::Coin> getCoins(const ECC::Amount& amount) = 0;
        virtual ~IKeyChain(){}
    };
}
