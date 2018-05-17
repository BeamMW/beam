#include "private_key.h"

// Valdo's point generator of elliptic curve
/*
namespace ECC {
  Context g_Ctx;
  const Context& Context::get() { return g_Ctx; }
}
*/

Scalar get_next_key(unsigned int id, Nonce& nonce) {

    ScalarValue pk_val = id;

    pk_val += nonce.get_init_state();

    nonce.inc();

    return Scalar(pk_val);

}

// Nonce implementation
ScalarValue Nonce::get() {

    ScalarValue current = init_state;
    ScalarValue nonce   = counter++;

    current += nonce;

    return current;
}

ScalarValue Nonce::get_init_state() {
    return init_state;
}

void Nonce::reset() {
    counter = 0;
}
// Nonce

