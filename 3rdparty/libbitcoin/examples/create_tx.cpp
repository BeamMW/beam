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

#include <bitcoin/bitcoin.hpp>
#include <iostream>


using namespace libbitcoin;
using namespace libbitcoin::wallet;
using namespace libbitcoin::chain;

std::string createTx()
{
    payment_address destinationAddress("miVBRoTahqyw4RqL5yhnwuVqkNDhfVgcxe");

    hash_digest utxoHash1;
    decode_hash(utxoHash1, "37ecad84a0a16f786a6e9fa8250c5f6efb61f0f1b6cc6dc0021959b725df3231");
    output_point utxo1(utxoHash1, 1);
    ec_private privateKey1("cVHGhFtxzhUg8yupEY574qJJXq8WjJnPSAhXPVjqvULLkB5HMcGo", ec_private::testnet);
    ec_public pubKey1 = privateKey1.to_public();
    std::cout << "addr1 = " << pubKey1.to_payment_address(ec_private::testnet).encoded() << std::endl;
    std::cout << "addr1 = " << pubKey1.to_payment_address().encoded() << std::endl;
    script lockingScript1 = script().to_pay_key_hash_pattern(pubKey1.to_payment_address(ec_private::testnet).hash());
    input input1 = input();
    input1.set_previous_output(utxo1);
    input1.set_sequence(4294967294);

    hash_digest utxoHash2;
    decode_hash(utxoHash2, "4aaa618240c49f8e16ddba95284980e68d211c81838600ed4fdf7124630a773a");
    output_point utxo2(utxoHash2, 0);
    ec_private privateKey2("cR4L1TJRSAp5y5B9DCr87PAx7YWnrL5oDknAXPESVff4HbjJL16h", ec_private::testnet);
    ec_public pubKey2 = privateKey2.to_public();
    script lockingScript2 = script().to_pay_key_hash_pattern(pubKey2.to_payment_address(ec_private::testnet).hash());
    input input2;
    input2.set_previous_output(utxo2);
    input2.set_sequence(4294967294);

    script outputScript = script().to_pay_key_hash_pattern(destinationAddress.hash());
    output output1(40 * satoshi_per_bitcoin - 500, outputScript);

    transaction tx;
    tx.inputs().push_back(input1);
    tx.inputs().push_back(input2);
    tx.outputs().push_back(output1);

    tx.set_version(2);

    endorsement sig1;
    if (lockingScript1.create_endorsement(sig1, privateKey1.secret(), lockingScript1, tx, 0u, machine::sighash_algorithm::all))
    {
        std::cout << "Signature: " << std::endl;
        std::cout << encode_base16(sig1) << "\n" << std::endl;
    }

    script::operation::list sigScript1;
    sigScript1.push_back(script::operation(sig1));
    data_chunk tmp;
    pubKey1.to_data(tmp);
    sigScript1.push_back(script::operation(tmp));
    script unlockingScript1(sigScript1);

    tx.inputs()[0].set_script(unlockingScript1);

    endorsement sig2;
    if (lockingScript2.create_endorsement(sig2, privateKey2.secret(), lockingScript2, tx, 1u, machine::sighash_algorithm::all))
    {
        std::cout << "Signature: " << std::endl;
        std::cout << encode_base16(sig2) << "\n" << std::endl;
    }

    script::operation::list sigScript2;
    sigScript2.push_back(script::operation(sig2));
    pubKey2.to_data(tmp);
    sigScript2.push_back(script::operation(tmp));
    script unlockingScript2(sigScript2);

    tx.inputs()[1].set_script(unlockingScript2);

    return encode_base16(tx.to_data());
}

void parseTx(const std::string& strTx)
{
    data_chunk txData;
    decode_base16(txData, strTx);
    transaction tx = transaction::factory_from_data(txData);

    for (size_t ind = 0; ind < tx.inputs().size(); ++ind)
    {
        auto inp = tx.inputs()[ind];
        auto h = encode_hash(inp.previous_output().hash());

        std::cout << "hash = " << h << std::endl;
        //payment_address addr(inp.script());

        //std::cout << "address = " << addr.encoded() << std::endl;

        auto addresses = inp.addresses();

        for (auto addr : addresses)
            std::cout << "address = " << addr.encoded() << std::endl;
    }
}

int main()
{
    std::string strTx = createTx();

    std::cout << strTx << std::endl;

    parseTx(strTx);

    return 0;
}