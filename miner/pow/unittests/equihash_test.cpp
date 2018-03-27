#include "../equihash.h"

#include <iostream>

// TODO: use boost tests here
// and call all the tests on Travis CI

int main(int argc, char *argv[])
{
    std::cout << "start miner test!!!\n"
              << std::endl;

    pow::Input input{1, 2, 3, 4, 56};

    pow::uint256_t nonce{1, 2, 4};
    auto proof = pow::get_solution(input, nonce);
    std::cout << proof.solution.size() << std::endl;
    if (pow::is_valid_proof(input, proof))
    {
        std::cout << "Valid solution found" << std::endl;
    }

    return 0;
}
