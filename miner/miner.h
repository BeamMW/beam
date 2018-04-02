#pragma once

#include "core/common.h"

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
    BlockPtr createBlock(const Block::Header& prevHeader);
private:
    Chain& m_blockChain;
    TxPool& m_txPool;
};

}
