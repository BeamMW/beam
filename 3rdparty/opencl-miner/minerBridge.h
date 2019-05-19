// BEAM OpenCL Miner
// Miner<->stratum bridge class
// Copyright 2018 The Beam Team
// Copyright 2018 Wilke Trei
#pragma once
#include <vector>
#include <stdint.h>

namespace beamMiner {

class minerBridge {
public:
    virtual ~minerBridge() = default;

    virtual bool hasWork() = 0;
    virtual void getWork(int64_t*, uint64_t*, uint8_t*, uint32_t*) = 0;

    virtual void handleSolution(int64_t&, uint64_t&, std::vector<uint32_t>&, uint32_t) = 0;
};

}
