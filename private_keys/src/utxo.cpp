#include "utxo.h"

// UTXO implementation
KeyGenerator UTXO::keygen = KeyGenerator();

void UTXO::init_keygen(KeyPhrase some_users_phrase) {
    keygen = KeyGenerator(some_users_phrase);

    std::ofstream os = create_out_filestream("./keygen.bin");
    keygen.write(os, some_users_phrase);
    os.close();
}

Scalar UTXO::get_amount_coins() {

    Public* ptr = m_pPublic.get();

    ScalarValue sv = ptr->m_Value;
    Scalar s       = sv;

    return s;
}

Scalar UTXO::get_blinding_factor() {
    return key.get();
}

void UTXO::write(std::ofstream &os) {
	os.write(reinterpret_cast<char*>(this), SIZEUTXO);
};

void UTXO::write(std::ofstream &os, const char* key) {
    char* encoded = encode(this, key);
	os.write(encoded, SIZEUTXO);
};

UTXO* UTXO::recover(std::ifstream &is, size_t offset) {
    UTXO* out = recover_from<UTXO>(is, offset);
    return out;
}

UTXO* UTXO::recover(std::ifstream &is, size_t offset, const char* key) {
    UTXO* out = recover_from<UTXO>(is, offset, key);
    return out;
}
// UTXO
