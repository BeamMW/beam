#include "miner/pow/equihash.h"
#include <iostream>

int main()
{
    beam::ByteBuffer input{1, 2, 3, 4, 56};

    beam::uint256_t nonce{1, 2, 4};
    auto proof = equi::getSolution(input, nonce);
    std::cout << proof.solution.size() << std::endl;

    if (equi::isValidProof(input, proof))
    {
        std::cout << "Solution is correct\n";
        return 0;
    }
    return -1;
}
