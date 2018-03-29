#include "pool/pool.h"
#include <iostream>

int main()
{
    beam::TxPool pool;
    auto tx = pool.getMinableTransactions();
    return 0;
}
