#pragma once

#include "core/common.h"

namespace beam
{
    class Chain
    {
    public:
        Chain();
        const Block& getHeadBlock() const;
        void processBlock(BlockPtr&& block);
    private:
        // TODO: replace
        std::vector<BlockPtr> m_blockChain;
    };
}