#include "../equihash.h"

#include <iostream> 

// TODO: use boost tests here
// and call all the tests on Travis CI

int main(int argc, char *argv[])
{
    std::cout << "start miner test!!!\n"
              << std::endl;

    pow::Equihash equi;
    equi.solve();

    return equi.solve() ? 0 : 1;
}
