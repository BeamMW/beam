#include "../equihash.h"

// TODO: use boost tests here
// and call all the tests on Travis CI

int main(int argc, char* argv[])
{
    Equihash equi;
    equi.solve();

    return 0;
}
