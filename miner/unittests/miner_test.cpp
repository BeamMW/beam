#include "miner/miner.h"
#include "chain/chain.h"
#include "pool/txpool.h"
#include <iostream>

int main()
{
    using namespace beam;
    Chain chain;
    TxPool txPool;
    Miner miner{ chain, txPool };

    miner.mineBlock();

    return 0;
}
