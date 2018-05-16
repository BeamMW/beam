#include "keychain.h"
#include "utility/logger.h"
#include "sqlite/sqlite3.h"
#include "wallet/private_key.h"

#include <boost/filesystem.hpp>

namespace beam
{
	const char* Keychain::getName()
	{
		return "wallet.dat";
	}

    IKeyChain::Ptr Keychain::init(const std::string& password)
    {
        if (!boost::filesystem::exists(getName()))
        {
            std::shared_ptr<Keychain> keychain = std::make_shared<Keychain>(password);

			int ret = sqlite3_open_v2(getName(), &keychain->_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_CREATE, NULL);
			assert(ret == SQLITE_OK);

			ret = sqlite3_key(keychain->_db, password.c_str(), password.size());
			assert(ret == SQLITE_OK);

			ret = sqlite3_exec(keychain->_db, "CREATE TABLE storage (id INTEGER PRIMARY KEY AUTOINCREMENT, amount INTEGER, status INTEGER);", NULL, NULL, NULL);
			assert(ret == SQLITE_OK);

            return std::static_pointer_cast<IKeyChain>(keychain);
        }

        LOG_ERROR() << getName() << " already exists.";

        return Ptr();
    }

    IKeyChain::Ptr Keychain::open(const std::string& password)
    {
        if (boost::filesystem::exists(getName()))
        {
            std::shared_ptr<Keychain> keychain = std::make_shared<Keychain>(password);

			int ret = sqlite3_open_v2(getName(), &keychain->_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX, NULL);
			assert(ret == SQLITE_OK);

			ret = sqlite3_key(keychain->_db, password.c_str(), password.size());
			assert(ret == SQLITE_OK);

            ret = sqlite3_exec(keychain->_db, "SELECT name FROM sqlite_master WHERE type='table' AND name='storage';", NULL, NULL, NULL);
            if(ret != SQLITE_OK)
            {
				LOG_ERROR() << "Invalid DB or wrong password :(";
                return Ptr();
            }

            return std::static_pointer_cast<IKeyChain>(keychain);
        }

		LOG_ERROR() << getName() << " not found, please init the wallet before.";

        return Ptr();
    }

    Keychain::Keychain(const std::string& pass)
        : _db(nullptr)
		, _nonce(std::make_shared<Nonce>(pass.c_str()))
    {
		
    }

    Keychain::~Keychain()
    {
        if(_db)
        {
		    sqlite3_close_v2(_db);
            _db = nullptr;
        }
    }

    uint64_t Keychain::getNextID()
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

    ECC::Scalar Keychain::calcKey(uint64_t id)
    {
        return get_next_key(id, *_nonce);
    }

    std::vector<beam::Coin> Keychain::getCoins(const ECC::Amount& amount, bool lock)
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

    void Keychain::store(const beam::Coin& coin)
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

    void Keychain::update(const std::vector<beam::Coin>& coins)
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

    void Keychain::remove(const std::vector<beam::Coin>& coins)
    {
		std::for_each(coins.begin(), coins.end(), [](const beam::Coin& coin)
		{
			// TODO: remove coin or change status to Unconfrmed/Spent ???
		});
    }
}