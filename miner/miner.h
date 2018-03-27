#pragma once

#include "global.h"

class Equihash;

class Miner
{
public:
    Miner();
    
    void mine();
private:
    std::shared_ptr<Equihash> pow;
};
