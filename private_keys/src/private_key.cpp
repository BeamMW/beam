#include "private_key.h"

// Valdo's point generator of elliptic curve
namespace ECC {
  Context g_Ctx;
  const Context& Context::get() { return g_Ctx; }
}

bool operator==(const Scalar& left, const Scalar& rigth) {

    ScalarValue v1 = left;
    ScalarValue v2 = rigth;

    return v1 == v2;
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

Scalar PrivateKeyScalar::get() const {
    return key;
}
// PrivateKeyScalar

// PrivateKey implementation
Scalar PrivateKey::get() {
    return key;
}

bool PrivateKey::cmp(const PrivateKey& other) const {

    ScalarValue v1 = key;
    ScalarValue v2 = other.key;

    return v1 == v2;
}


std::vector<PrivateKey> PrivateKey::create_keyset(PointGen& gen,
                                                  const PrivateKeyScalar scalar,
                                                  Nonce& nonce,
                                                  size_t count_key)
{
    std::vector<PrivateKey> keyset;
    keyset.reserve(count_key);

    for(size_t i=0; i<count_key; ++i)
        keyset.push_back(PrivateKey(gen, scalar, nonce));
}
// PrivateKey

// KeyGenerator implementation
PrivateKey KeyGenerator::next(){
    return PrivateKey(point_gen, scalar, nonce);
}

void KeyGenerator::reset() {
    nonce.reset();
}

void KeyGenerator::write(std::ofstream &os) {
    reset();
	os.write(reinterpret_cast<char*>(this), SIZKEYGEN);
};

void KeyGenerator::write(std::ofstream &os, const char* key) {
    reset();
    char* buf = encode(this, key);
	os.write(buf, SIZKEYGEN);
}

KeyGenerator* KeyGenerator::recover(std::ifstream &is) {
    KeyGenerator* out = recover_from<KeyGenerator>(is, 0);
    return out;
}

KeyGenerator* KeyGenerator::recover(std::ifstream &is, const char* key) {
    KeyGenerator* out = recover_from<KeyGenerator>(is, 0, key);
    return out;
}
// KeyGenerator
