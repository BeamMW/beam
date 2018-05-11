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

#include "wallet/keychain.h"

constexpr const size_t BUFSIZE = 32;

// Prototype of CoinData
struct CoinData : beam::Coin {

    unsigned int id;
    char info[BUFSIZE+1];

    CoinData() = default;

    // For testing
    CoinData(unsigned int num, char* data) : id(num) {

        auto n = std::min(strlen(data)+1, BUFSIZE+1);

		for(int i=0; i<n; ++i) info[i] = data[i];
		info[n] = '\0';
    }

    // Create CoinData with known amount of coins
    CoinData(const ECC::Amount& amount) 
        : beam::Coin(keygen.next().get(), amount)
    {
    }

    static void init_keygen(KeyPhrase some_users_phrase);
    static KeyGenerator get_keygen();

    ECC::Amount get_amount_coins();
    // Returns private key of this Coin
    Scalar get_blinding_factor();

    // Encrypt Coin and write it to filestream
    void write(std::ofstream &os, const char* key);
    // Write Coin to filestream
    void write(std::ofstream &os);

    static CoinData* recover(std::ifstream &is, size_t offset);
    static CoinData* recover(std::ifstream &is, size_t offset, const char* key);

    virtual ~CoinData() {}

    static KeyGenerator keygen;
};

constexpr size_t SIZE_COIN_DATA = sizeof(CoinData);

#endif // UTXO_INCLUDED
