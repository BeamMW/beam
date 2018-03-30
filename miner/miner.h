#pragma once

#include "core/common.h"
#include "core/block.h"

namespace beam
{

class Chain;
class TxPool;
class Miner
{
public:
    Miner(Chain& blockChain, TxPool& txPool);

    void start();
    void mineBlock();
private:
    BlockUniquePtr createBlock(const BlockHeader& prevHeader);
private:
    Chain& m_blockChain;
    TxPool& m_txPool;
};

}
