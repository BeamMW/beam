// Copyright 2019 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "utility/logger.h"

#include "bitcoin/bitcoin.hpp"

#include <iostream>

using namespace beam;
using namespace libbitcoin;
using namespace libbitcoin::wallet;


int main() {
    int logLevel = LOG_LEVEL_DEBUG;
    auto logger = Logger::create(logLevel, logLevel);

    //word_list my_word_list{ "market", "parent", "marriage", "drive", "umbrella", "custom", "leisure", "fury", "recipe", "steak", "have", "enable" };
    word_list my_word_list{ "child", "happy", "moment", "weird", "ten", "token", "stuff", "surface", "success", "desk", "embark", "observe" };

    auto hd_seed = electrum::decode_mnemonic(my_word_list);
    data_chunk seed_chunk(to_chunk(hd_seed));
    hd_private masterPrivateKey(seed_chunk, hd_public::testnet);
    hd_public masterPublicKey(masterPrivateKey.to_public());

    std::cout << "masterPublicKey = " << masterPublicKey.encoded() << std::endl;

    auto m0 = masterPrivateKey.derive_private(0);
    auto m1 = masterPrivateKey.derive_private(1);
    auto m2 = masterPrivateKey.derive_private(2);

    std::cout << "m0 = " << m0.to_public().encoded() << std::endl;
    std::cout << "m1 = " << m1.to_public() << std::endl;
    std::cout << "m2 = " << m2.to_public() << std::endl;

    //auto my_pubkeyhash = bitcoin_short_hash(m0.to_public().point());
    ec_public publicKey0(m0.to_public().derive_public(0).point());
    ec_public publicKey1(m0.to_public().derive_public(1).point());

    std::cout << "addr = " << publicKey0.to_payment_address(ec_private::testnet).encoded() << std::endl;
    std::cout << "addr = " << publicKey1.to_payment_address(ec_private::testnet).encoded() << std::endl;

    ec_public publicKey01(m1.to_public().derive_public(0).point());
    ec_public publicKey11(m1.to_public().derive_public(1).point());

    std::cout << "change = " << publicKey01.to_payment_address(ec_private::testnet).encoded() << std::endl;
    std::cout << "change = " << publicKey11.to_payment_address(ec_private::testnet).encoded() << std::endl;

    return 0;
}