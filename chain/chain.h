#pragma once

#include "core/block.h"

namespace beam
{
    class Chain
    {
    public:
        const Block& getHeadBlock() const;
    private:
        // TODO: replace
        std::vector<Block> m_blockChain;
    };
}