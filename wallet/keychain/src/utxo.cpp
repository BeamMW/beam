#include "utxo.h"

// CoinData implementation
KeyGenerator CoinData::keygen;

void CoinData::init_keygen(KeyPhrase some_users_phrase) {
    keygen = KeyGenerator(some_users_phrase);

    std::ofstream os = create_out_filestream("./keygen.bin");
    keygen.write(os, some_users_phrase);
    os.close();
}

KeyGenerator CoinData::get_keygen() {
    return keygen;
}

ECC::Amount CoinData::get_amount_coins() {
    return m_amount;
}

Scalar CoinData::get_blinding_factor() {
    return m_key;
}

void CoinData::write(std::ofstream &os) {
	os.write(reinterpret_cast<char*>(this), SIZE_COIN_DATA);
};

void CoinData::write(std::ofstream &os, const char* key) {
    char* encoded = encode(this, key);
	os.write(encoded, SIZE_COIN_DATA);
};

CoinData* CoinData::recover(std::ifstream &is, size_t offset) {
    CoinData* out = recover_from<CoinData>(is, offset);
    return out;
}

CoinData* CoinData::recover(std::ifstream &is, size_t offset, const char* key) {
    CoinData* out = recover_from<CoinData>(is, offset, key);
    return out;
}
// CoinData
