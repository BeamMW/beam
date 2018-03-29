#include "miner/miner.h"
#include <iostream>

int main()
{
    beam::Miner miner;
    beam::BlockHeader header;
    
    beam::Block block = miner.createBlock(header);

    return 0;
}
