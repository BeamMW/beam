#pragma once

#include "core/common.h"

namespace beam
{
    class TxPool
    {
    public:
        std::vector<Transaction> getMinableTransactions(size_t maxCount = 5000);
    };
}