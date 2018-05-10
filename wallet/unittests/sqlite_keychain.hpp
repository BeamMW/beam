#pragma once

#include "wallet/keychain.h"
#include "sqlite/sqlite3.h"

#include <boost/filesystem.hpp>

#include <algorithm>

struct SqliteKeychain : beam::IKeyChain
{
	SqliteKeychain()
		: _db(nullptr)
	{
		static const char* Name = "wallet.dat";
		static const char* Pass = "pass";

		// create DB with password
		{
			if (boost::filesystem::exists(Name))
				boost::filesystem::remove(Name);

			int ret = sqlite3_open_v2(Name, &_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_CREATE, NULL);
			assert(ret == SQLITE_OK);

			ret = sqlite3_key(_db, Pass, strlen(Pass));
			assert(ret == SQLITE_OK);

			ret = sqlite3_exec(_db, "CREATE TABLE storage (id integer PRIMARY KEY AUTOINCREMENT, amount integer, status integer);", NULL, NULL, NULL); // IF NOT EXISTS
			assert(ret == SQLITE_OK);

			sqlite3_close_v2(_db);
		}

		{
			int ret = sqlite3_open_v2(Name, &_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX, NULL);
			assert(ret == SQLITE_OK);

			ret = sqlite3_key(_db, Pass, strlen(Pass));
			assert(ret == SQLITE_OK);
		}

		//ret = sqlite3_exec(_db, "INSERT INTO sqlite_sequence (name,seq) VALUES('storage', 18);" , NULL, NULL, NULL);
		//assert(ret == SQLITE_OK);
	}

	virtual ~SqliteKeychain()
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
		// TODO: calculate key by id
		return ECC::Scalar();
	}

	virtual std::vector<beam::Coin> getCoins(const ECC::Amount& amount, bool lock = true)
	{
		std::vector<beam::Coin> coins;

		sqlite3_stmt* stmt = nullptr;
		int ret = sqlite3_prepare_v2(_db, "SELECT * FROM storage WHERE status=1 ORDER BY amount ASC;", -1, &stmt, 0);
		assert(ret == SQLITE_OK);

		ECC::Amount sum = 0;

		while (true)
		{
			if (sum >= amount) break;

			if (sqlite3_step(stmt) == SQLITE_ROW)
			{
				beam::Coin coin;
				coin.m_id = sqlite3_column_int64(stmt, 0);
				coin.m_amount = sqlite3_column_int64(stmt, 1);
				coin.m_status = static_cast<beam::Coin::Status>(sqlite3_column_int(stmt, 2));

				if (coin.m_status == beam::Coin::Unspent)
				{
					coin.m_status = beam::Coin::Locked;

					coins.push_back(coin);
					sum += coin.m_amount;

					// TODO: change coin status to Locked in the DB
				}
			}
			else break;
		}

		sqlite3_finalize(stmt);

		return coins;
	}

	virtual void store(const beam::Coin& coin)
	{
		sqlite3_stmt* stm = nullptr;
		int ret = sqlite3_prepare_v2(_db, "INSERT INTO storage (amount, status) VALUES(?1, ?2);", -1, &stm, NULL);
		assert(ret == SQLITE_OK);

		sqlite3_bind_int64(stm, 1, coin.m_amount);
		sqlite3_bind_int(stm, 2, coin.m_status);

		ret = sqlite3_step(stm);
		assert(ret == SQLITE_DONE);

		sqlite3_finalize(stm);
	}

	virtual void update(const std::vector<beam::Coin>& coins)
	{
		std::for_each(coins.begin(), coins.end(), [&](const beam::Coin& coin)
		{
			sqlite3_stmt* stm = nullptr;
			int ret = sqlite3_prepare_v2(_db, "UPDATE storage SET amount=?2, status=?3 WHERE id=?1;", -1, &stm, NULL);
			assert(ret == SQLITE_OK);

			sqlite3_bind_int64(stm, 1, coin.m_id);
			sqlite3_bind_int64(stm, 2, coin.m_amount);
			sqlite3_bind_int(stm, 3, coin.m_status);

			ret = sqlite3_step(stm);
			assert(ret == SQLITE_DONE);

			sqlite3_finalize(stm);
		});
	}

	virtual void remove(const std::vector<beam::Coin>& coins)
	{
		std::for_each(coins.begin(), coins.end(), [](const beam::Coin& coin)
		{
			// TODO: remove coin or change status to Unconfrmed/Spent ???
		});
	}


private:
	sqlite3 * _db;
};
