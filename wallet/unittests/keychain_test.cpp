#include <assert.h>

#include "wallet/keychain.h"
#include "wallet/sqlite/sqlite3.h"

#include <boost/filesystem.hpp>

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

		virtual uint64_t getNextID()
		{
			sqlite3_stmt* stmt = nullptr;
			int ret = sqlite3_prepare_v2(_db, "SELECT seq FROM sqlite_sequence WHERE name = 'storage';", -1, &stmt, 0);
			assert(ret == SQLITE_OK);

			int lastId = 0;
			ret = sqlite3_step(stmt);

			assert(ret == SQLITE_ROW || ret == SQLITE_DONE);

			if (ret == SQLITE_ROW)
				lastId = sqlite3_column_int(stmt, 0);

			sqlite3_finalize(stmt);

			return ++lastId;
		}

		virtual ECC::Scalar calcKey(uint64_t id)
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

	assert(keychain.getNextID() == 1);

	keychain.addCoin(123, beam::Coin::Unspent);
	keychain.addCoin(456, beam::Coin::Unspent);

	assert(keychain.getNextID() == 3);
}

int main() {

	testKeychain();
}
