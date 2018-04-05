#pragma once

#include "core/common.h"
#include "chain/chain.h"
#include "pool/txpool.h"

namespace beam
{
    struct Node
    {
        struct Config
        {
            int port;
        };

        void listen(const Config& config);

    private:

        void handlePoolPush(const ByteBuffer& data);

    private:
        Chain chain;
        TxPool pool;
    };

}
