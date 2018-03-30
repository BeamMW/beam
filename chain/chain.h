#pragma once

#include "core/block.h"

namespace beam
{
    class Chain
    {
    public:
        Chain();
        const Block& getHeadBlock() const;
        void processBlock(BlockUniquePtr&& block);
    private:
        // TODO: replace
        std::vector<BlockUniquePtr> m_blockChain;
    };
}