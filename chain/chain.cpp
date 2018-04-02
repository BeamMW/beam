#include "chain.h"

namespace beam
{
    Chain::Chain()
    {
        m_blockChain.push_back(std::make_unique<Block>());
    }

    const Block& Chain::getHeadBlock() const
    {
        return *m_blockChain.back();
    }

    void Chain::processBlock(BlockPtr&& block)
    {
        m_blockChain.push_back(std::move(block));
    }
};