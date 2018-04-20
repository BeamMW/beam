#include <iostream>
#include "private_key.h"
#include "storage.h"

void test_storage(const char* filename) {

    UTXO u1(123, "Some secret data.");
    UTXO u2(1,   "File top secret #1!");
    UTXO u3(789, "bank account #879 with money");

    char* skey = create_some_secret_key();

    std::ofstream os = create_out_filestream(filename);

    u1.write(os);
    u2.write(os);
    u3.write(os);

    u1.write(os, skey);
    u2.write(os, skey);
    u3.write(os, skey);

    os.close();

    std::ifstream is(filename, std::fstream::in);

    UTXO* pu1 = UTXO::recover(is, 0*SIZEUTXO);
    UTXO* pu2 = UTXO::recover(is, 1*SIZEUTXO);
    UTXO* pu3 = UTXO::recover(is, 2*SIZEUTXO);

    std::cout << "After recover from file " << filename << "\n";
    std::cout << "UTXO #1: id = " << pu1->id << "; info = " << pu1->info << "\n";
    std::cout << "UTXO #2: id = " << pu2->id << "; info = " << pu2->info << "\n";
    std::cout << "UTXO #3: id = " << pu3->id << "; info = " << pu3->info << "\n\n";

    UTXO* pu4 = UTXO::recover(is, 3*SIZEUTXO, "other secret key #1");
    UTXO* pu5 = UTXO::recover(is, 4*SIZEUTXO, "other secret key #2");
    UTXO* pu6 = UTXO::recover(is, 5*SIZEUTXO, skey);

    std::cout << "After recover by key from file " << filename << "\n";
    std::cout << "UTXO #1 with INCORRECT key: id = " << pu4->id << "; info = " << pu4->info << "\n";
    std::cout << "UTXO #2 with INCORRECT key: id = " << pu5->id << "; info = " << pu5->info << "\n";
    std::cout << "UTXO #3 with CORRECT   key: id = " << pu6->id << "; info = " << pu6->info << "\n\n";

    is.close();
}

void test_keygenerator(const char* filename, const char* encode_key, const char* decode_key) {

    KeyGenerator key_gen_1("secret_word_to_initiate");

    auto key11 = key_gen_1.next();
    auto key21 = key_gen_1.next();
    auto key31 = key_gen_1.next();

    std::ofstream os = create_out_filestream(filename);
    key_gen_1.write(os, encode_key);

    os.close();

    std::ifstream is(filename, std::fstream::in);

    KeyGenerator* key_gen_2 = KeyGenerator::recover(is, decode_key);

    auto key12 = key_gen_2->next();
    auto key22 = key_gen_2->next();
    auto key32 = key_gen_2->next();

    is.close();

    if (encode_key == decode_key)
        std::cout << "\nComparing after recover key generator with CORRECT secret key from file " << filename << "\n";
    else
        std::cout << "\nComparing after recover key generator with INCORRECT secret key from file " << filename << "\n";

    std::cout << "second index = 1 = original generator;\nsecond index = 2 = recovered generator\n\n";

    if (key11.cmp(key12) == 0) std::cout << "key11 == key12\n";
    else std::cout << "key11 != key12\n";

    if (key21.cmp(key22) == 0) std::cout << "key21 == key22\n";
    else std::cout << "key21 != key22\n";

    if (key31.cmp(key32) == 0) std::cout << "key31 == key32\n";
    else std::cout << "key31 != key32\n";
}

int main() {
    test_storage("./utxo.bin");
    test_keygenerator("./keygen.bin", "secret key", "secret key");
    test_keygenerator("./keygen.bin", "secret key", "another key");
}


