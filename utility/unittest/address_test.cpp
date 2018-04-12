#include "utility/io/address.h"
#include <iostream>
#include <assert.h>

using namespace beam::io;
using namespace std;

void address_test() {
    Address a;
    a.resolve("google.com");
    Address b;
    b.resolve("google.com:666");
    cout << a.str() << " " << b.str() << endl;
    assert(a.port() == 0 && b.port() == 666 && a.ip() != 0 && b.ip() != 0);
    b.resolve("localhost");
    cout << b.str() << endl;
    assert(b == Address::LOCALHOST);
}

int main() {
    address_test();
}

