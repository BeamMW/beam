#pragma once

#include <vector>
#include <array>
#include <utility>
#include <cstdint>
#include <memory>

namespace beam
{
// 256 bit hash
using Hash = std::array<uint8_t, 256/8>;

using uint256_t = std::array<uint8_t, 256/8>;

using Timestamp = uint64_t;

using Difficulty = uint64_t;

using Solution = std::vector<uint8_t>;

using ByteBuffer = std::vector<uint8_t>;

struct Proof
{
    // Nonce increment used to mine this block.
    uint256_t nonce;

    Solution solution;
    
    Proof() = default;
    Proof(Proof&& other) : nonce(std::move(nonce)), solution(std::move(solution)) {}
    const Proof& operator=(Proof&& other) 
    {
        nonce = std::move(other.nonce);
        solution = std::move(other.solution);
        return *this;
    }
};

}