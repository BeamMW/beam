#include "private_key.h"

#include <fstream>
#include <chrono>
#include <random>
#include <limits>

// Valdo's point generator of elliptic curve
namespace ECC {
  Context g_Ctx;
  const Context& Context::get() { return g_Ctx; }
}


// Random generator implementation
unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
std::default_random_engine generator(seed);
std::uniform_int_distribution<int> distribution(0, std::numeric_limits<unsigned int>::max());

unsigned int get_random() {
    return (unsigned int) distribution(generator);
}
// Random generator


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


// PrivateKeyScalar implementation
PrivateKeyScalar& PrivateKeyScalar::operator += (Nonce& nonce) {

    ECC::Scalar::Native sn;
    sn.Import(key);

    sn += nonce.get();

    key = sn;

    return *this;
}

ScalarValue PrivateKeyScalar::getNative() const {

    ScalarValue sv;
    sv.Import(key);

    return sv;
}
// PrivateKeyScalar


// PrivateKeyPoint implementation
int PrivateKeyPoint::cmp(const PrivateKeyPoint& other) const {
    return value.cmp(other.value);
}


std::vector<PrivateKeyPoint> PrivateKeyPoint::create_keyset(PointGen& gen,
                                                            const PrivateKeyScalar scalar,
                                                            Nonce& nonce,
                                                            size_t count_key)
{
    std::vector<PrivateKeyPoint> keyset;
    keyset.reserve(count_key);

    for(size_t i=0; i<count_key; ++i)
        keyset.push_back(PrivateKeyPoint(gen, scalar, nonce));
}
// PrivateKeyPoint


// KeyGenerator implementation
PrivateKeyPoint KeyGenerator::next(){
    return PrivateKeyPoint(point_gen, scalar, nonce);
}

void KeyGenerator::reset() {
    nonce.reset();
}

void KeyGenerator::write(std::ofstream &os) {
    reset();
	os.write(reinterpret_cast<char*>(this), SIZKEYGEN);
};

KeyGenerator KeyGenerator::recover(std::ifstream &is) {

    KeyGenerator *out = new KeyGenerator();
    is.read(reinterpret_cast<char*>(out), SIZKEYGEN);

    return *out;
}
// KeyGenerator
