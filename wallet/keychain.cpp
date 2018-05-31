#include "keychain.h"
#include "utility/logger.h"
#include "sqlite/sqlite3.h"
#include "wallet/private_key.h"

#include <boost/filesystem.hpp>

#define NOSEP

// Coin ID fields
// id          - coin counter
// height      - block height where we got the coin
// key_type    - key type
//
// amount      - amount
// count       - number of coins with same commitment
// status      - spent/unspent/unconfirmed/locked
// lock_height - height where we can spend the coin
#define ENUM_STORAGE_FIELDS(each, sep) \
    each(1, id,			sep, INTEGER PRIMARY KEY AUTOINCREMENT) \
    each(2, amount,		sep, INTEGER) \
    each(3, status,		sep, INTEGER) \
    each(4, height,		sep, INTEGER) \
    each(5, key_type,	   , INTEGER) // last item without separator

#define LIST(num, name, sep, type) #name sep
#define LIST_WITH_TYPES(num, name, sep, type) #name " " #type sep

#define STM_BIND_LIST(num, name, sep, type) stm.bind(num, coin.m_ ## name);
#define STM_GET_LIST(num, name, sep, type) stm.get(num-1, coin.m_ ## name);

#define BIND_LIST(num, name, sep, type) "?" #num sep
#define SET_LIST(num, name, sep, type) #name "=?" #num sep

#define STORAGE_FIELDS ENUM_STORAGE_FIELDS(LIST, ", ")
#define STORAGE_NAME "storage"
#define VARIABLES_NAME "variables"

#define ENUM_VARIABLES_FIELDS(each, sep) \
    each(1, name, sep, TEXT UNIQUE) \
    each(2, value,   , BLOB)

#define VARIABLES_FIELDS ENUM_VARIABLES_FIELDS(LIST, ", ")

namespace
{
    const char* WalletSeed = "WalletSeed";
}

namespace beam
{
    namespace sqlite
    {

        struct Statement
        {
            Statement(sqlite3* db, const char* sql)
                : _db(db)
                , _stm(nullptr)
            {
                int ret = sqlite3_prepare_v2(_db, sql, -1, &_stm, NULL);
                assert(ret == SQLITE_OK);
            }

            void bind(int col, int val)
            {
                int ret = sqlite3_bind_int(_stm, col, val);
                assert(ret == SQLITE_OK);
            }

            void bind(int col, KeyType val)
            {
                int ret = sqlite3_bind_int(_stm, col, static_cast<int>(val));
                assert(ret == SQLITE_OK);
            }

            void bind(int col, uint64_t val)
            {
                int ret = sqlite3_bind_int64(_stm, col, val);
                assert(ret == SQLITE_OK);
            }

            void bind(int col, const void* blob, int size)
            {
                int ret = sqlite3_bind_blob(_stm, col, blob, size, NULL);
                assert(ret == SQLITE_OK);
            }

			void bind(int col, const char* val)
			{
				int ret = sqlite3_bind_text(_stm, col, val, -1, NULL);
				assert(ret == SQLITE_OK);
			}

            bool step()
            {
                int ret = sqlite3_step(_stm);
                assert(ret == SQLITE_ROW || ret == SQLITE_DONE);

                return ret == SQLITE_ROW;
            }

            void get(int col, uint64_t& val)
            {
                val = sqlite3_column_int64(_stm, col);
            }

            void get(int col, int& val)
            {
                val = sqlite3_column_int(_stm, col);
            }

            void get(int col, beam::Coin::Status& status)
            {
                status = static_cast<beam::Coin::Status>(sqlite3_column_int(_stm, col));
            }

            void get(int col, bool& val)
            {
                val = sqlite3_column_int(_stm, col) == 0 ? false : true;
            }

            void get(int col, void* blob, int& size)
            {
                size = sqlite3_column_bytes(_stm, col);
                const void* data = sqlite3_column_blob(_stm, col);

                if(data) std::memcpy(blob, data, size);
            }
        
            void get(int col, beam::KeyType& type)
            {
                type = static_cast<beam::KeyType>(sqlite3_column_int(_stm, col));
            }

            ~Statement()
            {
                sqlite3_finalize(_stm);
            }
        private:

            sqlite3 * _db;
            sqlite3_stmt* _stm;
        };

        struct Transaction
        {
            Transaction(sqlite3* db)
                : _db(db)
                , _commited(false)
                , _rollbacked(false)
            {
                begin();
            }

            ~Transaction()
            {
                if(!_commited && !_rollbacked)
                    rollback();
            }

            void begin()
            {
                int ret = sqlite3_exec(_db, "BEGIN;", NULL, NULL, NULL);
                assert(ret == SQLITE_OK);
            }

            void commit()
            {
                int ret = sqlite3_exec(_db, "COMMIT;", NULL, NULL, NULL);
                assert(ret == SQLITE_OK);

                _commited = true;
            }

            void rollback()
            {
                int ret = sqlite3_exec(_db, "ROLLBACK;", NULL, NULL, NULL);
                assert(ret == SQLITE_OK);

                _rollbacked = true;
            }
        private:
            sqlite3 * _db;
            bool _commited;
            bool _rollbacked;
        };
    }

    Coin::Coin()
		: m_status(Unspent)
    {

    }

    Coin::Coin(const Amount& amount, Status status, const Height& height, KeyType keyType)
        : m_id{0}
        , m_amount{amount}
        , m_status{status}
        , m_height{height}
        , m_key_type{ keyType }
    {

    }
    
    const char* Keychain::getName()
    {
        return "wallet.db";
    }

    IKeyChain::Ptr Keychain::init(const std::string& password, const ECC::NoLeak<ECC::uintBig>& secretKey)
    {
        if (!boost::filesystem::exists(getName()))
        {
            auto keychain = std::make_shared<Keychain>(password, secretKey);

            {
                int ret = sqlite3_open_v2(getName(), &keychain->_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_CREATE, NULL);
                assert(ret == SQLITE_OK);
            }

            {
                int ret = sqlite3_key(keychain->_db, password.c_str(), password.size());
                assert(ret == SQLITE_OK);
            }

            {
                const char* req = "CREATE TABLE " STORAGE_NAME " (" ENUM_STORAGE_FIELDS(LIST_WITH_TYPES, ", ") ");";
                int ret = sqlite3_exec(keychain->_db, req, NULL, NULL, NULL);
                assert(ret == SQLITE_OK);
            }

            {
                const char* req = "CREATE TABLE " VARIABLES_NAME " (" ENUM_VARIABLES_FIELDS(LIST_WITH_TYPES, ", ") ");";
                int ret = sqlite3_exec(keychain->_db, req, NULL, NULL, NULL);
                assert(ret == SQLITE_OK);
            }
            {
                keychain->setVar(WalletSeed, secretKey.V);
            }

            return std::static_pointer_cast<IKeyChain>(keychain);
        }

        LOG_ERROR() << getName() << " already exists.";

        return Ptr();
    }

    IKeyChain::Ptr Keychain::open(const std::string& password)
    {
        if (boost::filesystem::exists(getName()))
        {
            ECC::NoLeak<ECC::uintBig> seed;
            seed.V = ECC::Zero;
            auto keychain = std::make_shared<Keychain>(password, seed);

            {
                int ret = sqlite3_open_v2(getName(), &keychain->_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX, NULL);
                assert(ret == SQLITE_OK);
            }

            {
                int ret = sqlite3_key(keychain->_db, password.c_str(), password.size());
                assert(ret == SQLITE_OK);
            }

            {
                const char* req = "SELECT name FROM sqlite_master WHERE type='table' AND name='" STORAGE_NAME "';";
                int ret = sqlite3_exec(keychain->_db, req, NULL, NULL, NULL);
                if(ret != SQLITE_OK)
                {
                    LOG_ERROR() << "Invalid DB or wrong password :(";
                    return Ptr();
                }
            }

            {
                const char* req = "SELECT " STORAGE_FIELDS " FROM " STORAGE_NAME ";";
                int ret = sqlite3_exec(keychain->_db, req, NULL, NULL, NULL);
                if (ret != SQLITE_OK)
                {
                    LOG_ERROR() << "Invalid DB format :(";
                    return Ptr();
                }
            }

            {
                const char* req = "SELECT " VARIABLES_FIELDS " FROM " VARIABLES_NAME ";";
                int ret = sqlite3_exec(keychain->_db, req, NULL, NULL, NULL);
                if (ret != SQLITE_OK)
                {
                    LOG_ERROR() << "Invalid DB format :(";
                    return Ptr();
                }
            }
            
            if (keychain->getVar(WalletSeed, seed))
            {
                keychain->m_kdf.m_Secret = seed;
            }
            else
            {
                assert(false && "there is no seed for keychain");
            }

            return std::static_pointer_cast<IKeyChain>(keychain);
        }

        LOG_ERROR() << getName() << " not found, please init the wallet before.";

        return Ptr();
    }

    Keychain::Keychain(const std::string& pass, const ECC::NoLeak<ECC::uintBig>& secretKey)
        : _db(nullptr)
        , _nonce(std::make_shared<Nonce>(pass.c_str()))
    {
        m_kdf.m_Secret = secretKey;
    }

    Keychain::~Keychain()
    {
        if(_db)
        {
            sqlite3_close_v2(_db);
            _db = nullptr;
        }
    }

	uint64_t getLastID(sqlite3* db)
	{
	    int lastId = 0;

	    {
	        const char* req = "SELECT seq FROM sqlite_sequence WHERE name = '" STORAGE_NAME "';";
	        sqlite::Statement stm(db, req);

	        if (stm.step())
	            stm.get(0, lastId);
	    }

	    return lastId;
	}

    ECC::Scalar::Native Keychain::calcKey(const beam::Coin& coin) const
    {
        ECC::Scalar::Native key;
        // For coinbase and free commitments we generate key as function of (height and type), for regular coins we add id, to solve collisions
        DeriveKey(key, m_kdf, coin.m_height, coin.m_key_type, (coin.m_key_type == KeyType::Regular) ? coin.m_id : 0);
        return key;
    }

    std::vector<beam::Coin> Keychain::getCoins(const ECC::Amount& amount, bool lock)
    {
        std::vector<beam::Coin> coins;

        ECC::Amount sum = 0;

        {
            const char* req = "SELECT " STORAGE_FIELDS " FROM " STORAGE_NAME " WHERE status=?1 ORDER BY amount ASC;";
            sqlite::Statement stm(_db, req);
            stm.bind(1, Coin::Unspent);

			Block::SystemState::ID stateID = { 0 };
			getSystemStateID(stateID);

            while (true)
            {
                if (sum >= amount) break;

                if (stm.step())
                {
                    beam::Coin coin;

                    ENUM_STORAGE_FIELDS(STM_GET_LIST, NOSEP);

                    if (coin.m_status == beam::Coin::Unspent)
                    {
						Height lockHeight = coin.m_height + (coin.m_key_type == KeyType::Coinbase 
							? Block::Rules::MaturityCoinbase 
							: Block::Rules::MaturityStd);

						if (lockHeight >= stateID.m_Height)
						{
							if (lock)
							{
								coin.m_status = beam::Coin::Locked;
							}

							coins.push_back(coin);
							sum += coin.m_amount;
						}
                    }
                }
                else break;
            }
        }

        if (sum < amount)
        {
            coins.clear();
        }
        else if(lock)
        {
            sqlite::Transaction trans(_db);

            for (const auto& coin : coins)
            {
                const char* req = "UPDATE " STORAGE_NAME " SET status=?2 WHERE id=?1;";
                sqlite::Statement stm(_db, req);

                stm.bind(1, coin.m_id);
                stm.bind(2, coin.m_status);

                stm.step();
            }

            trans.commit();
        }

        return coins;
    }

    void Keychain::store(beam::Coin& coin)
    {
        sqlite::Transaction trans(_db);
        
        {
            const char* req = "INSERT INTO " STORAGE_NAME " (amount, status, height, key_type) VALUES(?1, ?2, ?3, ?4);";
            sqlite::Statement stm(_db, req);

			stm.bind(1, coin.m_amount);
			stm.bind(2, coin.m_status);
			stm.bind(3, coin.m_height);
			stm.bind(4, coin.m_key_type);

            stm.step();
        }

        trans.commit();

		coin.m_id = getLastID(_db);
    }

    void Keychain::store(std::vector<beam::Coin>& coins)
    {
        if (coins.empty()) return;
        
        sqlite::Transaction trans(_db);
        for (auto& coin : coins)
        {
			const char* req = "INSERT INTO " STORAGE_NAME " (amount, status, height, key_type) VALUES(?1, ?2, ?3, ?4);";
			sqlite::Statement stm(_db, req);

			stm.bind(1, coin.m_amount);
			stm.bind(2, coin.m_status);
			stm.bind(3, coin.m_height);
			stm.bind(4, coin.m_key_type);

            stm.step();

			coin.m_id = getLastID(_db);
        }

        trans.commit();
    }

    void Keychain::update(const std::vector<beam::Coin>& coins)
    {
        if (coins.size())
        {
            sqlite::Transaction trans(_db);

            for(const auto& coin : coins)
            {
                const char* req = "UPDATE " STORAGE_NAME " SET " ENUM_STORAGE_FIELDS(SET_LIST, ", ") " WHERE id=?1;";
                sqlite::Statement stm(_db, req);

                ENUM_STORAGE_FIELDS(STM_BIND_LIST, NOSEP);

                stm.step();
            }

            trans.commit();
        }
    }

    void Keychain::remove(const std::vector<beam::Coin>& coins)
    {
        if (coins.size())
        {
            sqlite::Transaction trans(_db);

            for (const auto& coin : coins)
            {
                const char* req = "DELETE FROM " STORAGE_NAME " WHERE id=?1;";
                sqlite::Statement stm(_db, req);

                stm.bind(1, coin.m_id);

                stm.step();
            }

            trans.commit();
        }
    }

    void Keychain::remove(const beam::Coin& coin)
    {
        sqlite::Transaction trans(_db);

        const char* req = "DELETE FROM " STORAGE_NAME " WHERE id=?1;";
        sqlite::Statement stm(_db, req);

        stm.bind(1, coin.m_id);

        stm.step();
        trans.commit();
    }

    void Keychain::visit(std::function<bool(const beam::Coin& coin)> func)
    {
        const char* req = "SELECT " STORAGE_FIELDS " FROM " STORAGE_NAME ";";
        sqlite::Statement stm(_db, req);

        while (stm.step())
        {
            Coin coin;

            ENUM_STORAGE_FIELDS(STM_GET_LIST, NOSEP);

            if (!func(coin))
                break;
        }
    }

    void Keychain::setVarRaw(const char* name, const void* data, int size)
    {
        sqlite::Transaction trans(_db);

        {
			const char* req = "INSERT or REPLACE INTO " VARIABLES_NAME " (" VARIABLES_FIELDS ") VALUES(?1, ?2);";

            sqlite::Statement stm(_db, req);
			
            stm.bind(1, name);
            stm.bind(2, data, size);

            stm.step();
        }

        trans.commit();
    }

    int Keychain::getVarRaw(const char* name, void* data) const
    {
        const char* req = "SELECT value FROM " VARIABLES_NAME " WHERE name=?1;";

        sqlite::Statement stm(_db, req);
		stm.bind(1, name);
        stm.step();

        int size = 0;
        stm.get(0, data, size);

        return size;
    }

	const char* SystemStateIDName = "SystemStateID";

	void Keychain::setSystemStateID(const Block::SystemState::ID& stateID)
	{
		setVar(SystemStateIDName, stateID);
	}

	bool Keychain::getSystemStateID(Block::SystemState::ID& stateID) const
	{
		return getVar(SystemStateIDName, stateID);
	}

    Height Keychain::getCurrentHeight() const
    {
        Block::SystemState::ID id = { 0 };
        if (getSystemStateID(id))
        {
            return id.m_Height;
        }
        return 0;
    }

}