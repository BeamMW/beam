#pragma once

#include "core/common.h"
#include "chain/chain.h"
#include "pool/txpool.h"

namespace beam
{
    struct NetworkToWallet
    {
        virtual void onNewTransaction(const Transaction& tx) = 0;
    };

    struct Node : public NetworkToWallet
    {

        struct Config
        {
            int port;
        };

        void listen(const Config& config);

        virtual void onNewTransaction(const Transaction& tx);

    private:
        Chain chain;
        TxPool pool;
    };

}
