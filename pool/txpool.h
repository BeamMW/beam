#pragma once

#include "core/transaction.h"

namespace beam
{
    class TxPool
    {
    public:
        std::vector<Transaction> getMinableTransaction(size_t maxCount = 5000);
    };
}