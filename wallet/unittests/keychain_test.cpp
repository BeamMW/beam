#include <iostream>
#include <sstream>
#include <assert.h>
#include "private_key.h"
#include "utill.h"
#include "coin.h"

#include "wallet/keychain.h"
#include "wallet/sqlite/sqlite3.h"

#include <boost/filesystem.hpp>

void test_CoinData(const char* filename) {

    std::cout << "Test #1 is working...\n";

    CoinData u1(123, "Some secret data.");
    CoinData u2(1,   "File top secret #1!");
    CoinData u3(789, "bank account #879 with money");

    std::unique_ptr<char> skey(create_some_secret_key());

    std::ofstream os = create_out_filestream(filename);

    u1.write(os);
    u2.write(os);
    u3.write(os);

    u1.write(os, skey.get());
    u2.write(os, skey.get());
    u3.write(os, skey.get());

    os.close();

    std::ifstream is(filename, std::fstream::in);

    std::unique_ptr<CoinData> pu1(CoinData::recover(is, 0*SIZE_COIN_DATA));
    std::unique_ptr<CoinData> pu2(CoinData::recover(is, 1*SIZE_COIN_DATA));
    std::unique_ptr<CoinData> pu3(CoinData::recover(is, 2*SIZE_COIN_DATA));

    std::cout << "After recover from file " << filename << "\n";
    std::cout << "CoinData #1: id = " << pu1->id << "; info = " << pu1->info << "\n";
    std::cout << "CoinData #2: id = " << pu2->id << "; info = " << pu2->info << "\n";
    std::cout << "CoinData #3: id = " << pu3->id << "; info = " << pu3->info << "\n\n";

    CoinData* pu4(CoinData::recover(is, 3*SIZE_COIN_DATA, "other secret key #1"));
    CoinData* pu5(CoinData::recover(is, 4*SIZE_COIN_DATA, "other secret key #2"));
    std::unique_ptr<CoinData> pu6(CoinData::recover(is, 5*SIZE_COIN_DATA, skey.get()));

    std::cout << "After recover by key from file " << filename << "\n";
    std::cout << "CoinData #1 with INCORRECT key: id = " << pu4->id << "; info = " << pu4->info << "\n";
    std::cout << "CoinData #2 with INCORRECT key: id = " << pu5->id << "; info = " << pu5->info << "\n";
    std::cout << "CoinData #3 with CORRECT   key: id = " << pu6->id << "; info = " << pu6->info << "\n\n";

    is.close();

    std::cout << "Test #1 is done.\n\n";

    // delete coin data with invalid key
    delete[] reinterpret_cast<char*>(pu5);
    delete[] reinterpret_cast<char*>(pu4);
}

void test_keygenerator(const char* filename, const char* encode_key, const char* decode_key) {

    std::cout << "Test #2 is working\n";

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

    if (key11.cmp(key12)) std::cout << "key11 == key12\n";
    else std::cout << "key11 != key12\n";

    if (key21.cmp(key22)) std::cout << "key21 == key22\n";
    else std::cout << "key21 != key22\n";

    if (key31.cmp(key32)) std::cout << "key31 == key32\n";
    else std::cout << "key31 != key32\n";

    std::cout << "Test #2 is done.\n\n";
}

void test_key_CoinData() {

    std::cout << "Test #3 is working...\n";

    CoinData::init_keygen("some phrase to init");

    CoinData u1(123);
    std::unique_ptr<CoinData> pu2(std::make_unique<CoinData>(777));
    std::unique_ptr<CoinData> pu3(std::make_unique<CoinData>(122 + 1));

    Scalar key1 = u1.get_blinding_factor();
    Scalar key2 = pu2->get_blinding_factor();

    if (key1 == key2) std::cout << "key1 == key2\n";
    else std::cout << "key1 != key2\n";

    auto n1 = u1.get_amount_coins();
    auto n2 = pu2->get_amount_coins();
    auto n3 = pu3->get_amount_coins();

    assert(n1 == n3);

    KeyGenerator keygen = CoinData::get_keygen();

    std::cout << "Count of CoinData: " << keygen.get_count() << "\n";

    std::cout << "Test #3 is done.\n\n";
}

namespace
{
	struct TestKeychain : beam::IKeyChain
	{
		TestKeychain()
			: _db(nullptr)
		{
			static const char* Name = "wallet.dat";

			if (boost::filesystem::exists(Name))
				boost::filesystem::remove(Name);

			int ret = sqlite3_open_v2(Name, &_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_CREATE, NULL);
			assert(ret == SQLITE_OK);

			ret = sqlite3_exec(_db, "CREATE TABLE IF NOT EXISTS storage (id integer PRIMARY KEY AUTOINCREMENT, amount integer, status integer);", NULL, NULL, NULL);
			assert(ret == SQLITE_OK);

			//ret = sqlite3_exec(_db, "INSERT INTO sqlite_sequence (name,seq) VALUES('storage', 18);" , NULL, NULL, NULL);
			//assert(ret == SQLITE_OK);
		}

		virtual ~TestKeychain() 
		{
			sqlite3_close_v2(_db);
		}

		virtual ECC::Scalar getNextKey()
		{
			return ECC::Scalar();
		}

		virtual std::vector<beam::Coin> getCoins(const ECC::Amount& amount, bool lock = true)
		{
			return std::vector<beam::Coin>();
		}

		virtual void store(const beam::Coin& coin)
		{

		}

		virtual void update(const std::vector<beam::Coin>& coins)
		{

		}

		virtual void remove(const std::vector<beam::Coin>& coins)
		{

		}

		void addCoin(const ECC::Amount& amount, beam::Coin::Status status)
		{
			static const char* str = "INSERT INTO storage (amount, status) VALUES(?1, ?2);";
			sqlite3_stmt* stm = nullptr;
			int ret = sqlite3_prepare_v2(_db, str, strlen(str), &stm, NULL);
			assert(ret == SQLITE_OK);

			sqlite3_bind_int64(stm, 1, amount);
			assert(ret == SQLITE_OK);

			sqlite3_bind_int(stm, 2, status);
			assert(ret == SQLITE_OK);

			ret = sqlite3_step(stm);
			assert(ret == SQLITE_DONE);

			sqlite3_finalize(stm);
		}

	private:
		sqlite3* _db;
	};
}

void testKeychain()
{
	TestKeychain keychain;

	keychain.addCoin(123, beam::Coin::Unspent);
}

int main() {

	testKeychain();

    //test_CoinData("./coin_data.bin");

    //test_keygenerator("./keygen1.bin", "secret key", "secret key");
    //test_keygenerator("./keygen2.bin", "secret key", "another key");

    //test_key_CoinData();
}
