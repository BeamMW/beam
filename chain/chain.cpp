#include "chain.h"

namespace beam
{
    Chain::Chain()
    {
        m_blockChain.emplace_back(std::make_unique<Block>());
    }

    const Block& Chain::getHeadBlock() const
    {
        return *m_blockChain.back();
    }

    void Chain::processBlock(BlockUniquePtr&& block)
    {
        m_blockChain.emplace_back(std::move(block));
    }
};