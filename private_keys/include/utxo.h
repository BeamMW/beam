#ifndef UTXO_INCLUDED
#define UTXO_INCLUDED

#include <stdio.h>
#include <fstream>
#include <algorithm>
#include <string.h>

#include "../core/common.h"
#include "../core/ecc.h"
#include "private_key.h"
#include "utill.h"

constexpr const size_t BUFSIZE = 32;

// Prototype of UTXO
struct UTXO : beam::Output {

    using Public = ECC::RangeProof::Public;

    unsigned int id;
    char info[BUFSIZE+1];

    UTXO() = default;

    // For testing
    UTXO(unsigned int num, char* data) : id(num) {

        auto n = std::min(strlen(data)+1, BUFSIZE+1);

		for(int i=0; i<n; ++i) info[i] = data[i];
		info[n] = '\0';
    }

    // Create UTXO with known amount of coins
    UTXO(unsigned int coins) {

        Public* ptr_pub = new Public;
        ptr_pub->m_Value = coins;

        m_pPublic = std::unique_ptr<Public>(ptr_pub);

        key = keygen.next();
    }

    static void init_keygen(KeyPhrase some_users_phrase);
    static KeyGenerator get_keygen();

    Scalar get_amount_coins();
    // Returns private key of this UTXO
    Scalar get_blinding_factor();

    // Encrypt UTXO and write it to filestream
    void write(std::ofstream &os, const char* key);
    // Write UTXO to filestream
    void write(std::ofstream &os);

    static UTXO* recover(std::ifstream &is, size_t offset);
    static UTXO* recover(std::ifstream &is, size_t offset, const char* key);

    virtual ~UTXO() {}

    private:
        static KeyGenerator keygen;
        PrivateKey key;

};

constexpr size_t SIZEUTXO = sizeof(UTXO);

#endif // UTXO_INCLUDED
