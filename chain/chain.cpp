#include "chain.h"

namespace beam
{
    const Block& Chain::getHeadBlock() const
    {
        return m_blockChain.back();
    }

    
};