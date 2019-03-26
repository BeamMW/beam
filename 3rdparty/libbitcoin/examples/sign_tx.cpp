// Copyright 2018 The Beam Team
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

#include <bitcoin/bitcoin.hpp>
#include <iostream>


int main()
{
    libbitcoin::data_chunk tx_data;
    libbitcoin::decode_base16(tx_data, "0200000001c18f05d42f69b2d0acf28e2eb2b992089f2ceb3cec83d20a837a493310fd60bc0100000000ffffffff01804a5d050000000017a914f1d14226a785b27796701e4bdeb5a620aa6ea7998700000000");

    libbitcoin::chain::transaction tx;
    tx.from_data(tx_data);

    libbitcoin::chain::script prevoutScript;
    prevoutScript.from_string("checksig");

    auto tmp = libbitcoin::base58_literal("cQSg1yRWuKNHZnpVHKECdVoDnafJN6hUeyTPD48Q5S4DEbUH4nkN");
    libbitcoin::wallet::ec_private ecPrivate(tmp, libbitcoin::wallet::ec_private::testnet_wif);
    libbitcoin::ec_secret secret = ecPrivate.secret();
    //const libbitcoin::ec_secret secret = libbitcoin::hash_literal("cQSg1yRWuKNHZnpVHKECdVoDnafJN6hUeyTPD48Q5S4DEbUH4nkN");

    libbitcoin::endorsement sign;

    libbitcoin::chain::script::create_endorsement(sign, secret, prevoutScript, tx, 0, libbitcoin::machine::sighash_algorithm::all);

    libbitcoin::chain::script::operation::list sigScriptOp;
    sigScriptOp.push_back(libbitcoin::chain::script::operation(sign));
    sigScriptOp.push_back(libbitcoin::chain::script::operation(libbitcoin::to_chunk(ecPrivate.to_public().encoded())));


    tx.inputs()[0].set_script(libbitcoin::chain::script(sigScriptOp));

    //libbitcoin::chain::script::verify(tc)

    std::cout << libbitcoin::encode_base16(sign) << "  " << ecPrivate.to_public() << std::endl;

    std::cout << libbitcoin::encode_base16(tx.to_data());
    return 0;
}