#pragma once

#include "core/common.h"
#include "core/block.h"

namespace beam
{

class Miner
{
public:
    Miner();

    void start();
    void mine();

    Block createBlock(const BlockHeader& prevHeader);

};

}
