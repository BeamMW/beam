#ifndef PRIVATE_KEY_INCLUDED
#define PRIVATE_KEY_INCLUDED

#include <string.h>
#include <vector>
#include <iostream>
#include <chrono>
#include <random>
#include <limits>

#include "core/ecc_native.h"
#include "core/ecc.h"

// Pseudonyms for usefull work
using ScalarValue = ECC::Scalar::Native;
using Scalar      = ECC::Scalar;
using Point       = ECC::Point;
using PointValue  = ECC::Point::Native;
using PointGen    = ECC::Context;

// Nonce is used to create private key assortiment with varying key values:
// Here the formula is used:
//
// point[id]       = (Scalar[id] + Nonce) * G,
// private key[id] =  point[id].X
//
// where G     - point-generator of elliptic curve and
//       Nonce - some value of scalar with specific rule of generating
//       id    - UTXO identificator
struct Nonce {

    Nonce() = default;

    Nonce(const char* keyphrase) : counter(0) {

        // ECC::Scalar::Native init state = convert2scalar(sha256(any user's key phrase))

        // 1. Create scalar with hash of some phrase
        ECC::Hash::Processor hash_processor;
        ECC::Hash::Value hash_value;

        hash_processor.Write(keyphrase, strlen(keyphrase));

        hash_processor >> hash_value;

        Scalar scalar;
        scalar.m_Value = hash_value;

        // 2. Multiply scalar and point-generator G
        PointGen gen;
        PointValue pG = gen.get().G * scalar; ;
        Point point = pG;

        // 3. Initialize state by coordinate X of point above
        scalar.m_Value = point.m_X;
        init_state = scalar;
    }

    Nonce& operator=(const Nonce& other) = default;

    ScalarValue get();
    ScalarValue get_init_state();

    uint64_t get_count() {
        return counter;
    }

    void inc() {
        counter++;
    }

    // Set counter of Nonce to zero
    void reset();

    private:
        ScalarValue init_state;
        uint64_t      counter;
};

Scalar get_next_key(unsigned int id, Nonce& nonce);

#endif // PRIVATE_KEY_INCLUDED
