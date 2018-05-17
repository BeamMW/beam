#include <iostream>
#include <assert.h>

#include "wallet/private_key.h"

void test1() {
    Nonce nonce("some key");

    Scalar pk1 = get_next_key(0, nonce);
    Scalar pk2 = get_next_key(0, nonce);
    Scalar pk3 = get_next_key(1, nonce);

    assert(pk1 == pk2);
    assert(!(pk1 == pk3));
    assert(nonce.get_count() == 3);

    std::cout << "Test is OK." << std::endl;
}

int main() {

    test1();

    return 0;
}
