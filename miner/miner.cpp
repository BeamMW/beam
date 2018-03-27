#include "miner.h"

#include "pow/equihash.h"

Miner::Miner()
{
}

void Miner::mine()
{
    pow::Input input{1, 2, 3, 4, 56};
    pow::uint256_t nonce{1, 2, 4};

    pow::is_valid_proof(input, pow::get_solution(input, nonce));
}
