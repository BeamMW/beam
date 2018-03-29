#include "miner.h"

#include "pow/equihash.h"

namespace beam
{

Miner::Miner()
{
}

void Miner::start()
{

}

void Miner::mine()
{
    equi::ByteBuffer input{1, 2, 3, 4, 56};
    beam::uint256_t nonce{1, 2, 4};

    equi::is_valid_proof(input, equi::get_solution(input, nonce));
}

Block Miner::createBlock(const BlockHeader& prevHeader)
{
    Block block;
    // TODO: get transactions
    // TODO: calc merkle root hash
    
    return block;
}

}
