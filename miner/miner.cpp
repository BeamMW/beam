#include "miner.h"

#include "pow/equihash.h"

Miner::Miner()
{
}

void Miner::mine()
{
    equi::Input input{1, 2, 3, 4, 56};
    equi::uint256_t nonce{1, 2, 4};

    equi::is_valid_proof(input, equi::get_solution(input, nonce));
}
