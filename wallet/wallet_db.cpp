#include "wallet_db.h"
#include "sender.h"
#include "utility/logger.h"
#include "sqlite/sqlite3.h"
#include <sstream>

#include <boost/filesystem.hpp>

#define NOSEP
#define COMMA ", "

// Coin ID fields
// id          - coin counter
// height      - block height where we got the coin(coinbase) or the height of the latest known block
// key_type    - key type
//
// amount      - amount
// count       - number of coins with same commitment
// status      - spent/unspent/unconfirmed/locked
// maturity    - height where we can spend the coin

#define ENUM_STORAGE_ID(each, sep, obj) \
    each(1, id, sep, INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, obj)

#define ENUM_STORAGE_FIELDS(each, sep, obj) \
    each(2, amount,     sep, INTEGER NOT NULL, obj) \
    each(3, status,     sep, INTEGER NOT NULL, obj) \
    each(4, height,     sep, INTEGER NOT NULL, obj) \
    each(5, maturity,   sep, INTEGER NOT NULL, obj) \
    each(6, key_type,   sep, INTEGER NOT NULL, obj) \
    each(7, createTxId, sep, BLOB, obj) \
    each(8, spentTxId,  , BLOB, obj)            // last item without separator

#define ENUM_ALL_STORAGE_FIELDS(each, sep, obj) \
    ENUM_STORAGE_ID(each, sep, obj) \
    ENUM_STORAGE_FIELDS(each, sep, obj)

#define LIST(num, name, sep, type, obj) #name sep
#define LIST_WITH_TYPES(num, name, sep, type, obj) #name " " #type sep

#define STM_BIND_LIST(num, name, sep, type, obj) stm.bind(num, obj .m_ ## name);
#define STM_GET_LIST(num, name, sep, type, obj) stm.get(num-1, obj .m_ ## name);

#define BIND_LIST(num, name, sep, type, obj) "?" #num sep
#define SET_LIST(num, name, sep, type, obj) #name "=?" #num sep

#define STORAGE_FIELDS ENUM_ALL_STORAGE_FIELDS(LIST, COMMA, )
#define STORAGE_NAME "storage"
#define VARIABLES_NAME "variables"
#define HISTORY_NAME "history"

#define ENUM_VARIABLES_FIELDS(each, sep, obj) \
    each(1, name, sep, TEXT UNIQUE, obj) \
    each(2, value,   , BLOB, obj)

#define VARIABLES_FIELDS ENUM_VARIABLES_FIELDS(LIST, COMMA, )

#define ENUM_HISTORY_FIELDS(each, sep, obj) \
    each(1, txId,       sep, BLOB NOT NULL PRIMARY KEY, obj) \
    each(2, amount,     sep, INTEGER NOT NULL, obj) \
    each(3, fee,        sep, INTEGER NOT NULL, obj) \
    each(4, peerId,     sep, INTEGER NOT NULL, obj) \
    each(5, message,    sep, BLOB, obj) \
    each(6, createTime, sep, INTEGER NOT NULL, obj) \
    each(7, modifyTime, sep, INTEGER, obj) \
    each(8, sender,     sep, INTEGER NOT NULL, obj) \
    each(9, status,     sep, INTEGER NOT NULL, obj) \
    each(10, fsmState,      , BLOB, obj)
#define HISTORY_FIELDS ENUM_HISTORY_FIELDS(LIST, COMMA, )

namespace beam
{
    using namespace std;

	namespace
	{
        void throwIfError(int res, sqlite3* db)
		{
			if (res == SQLITE_OK)
			{
				return;
			}
			stringstream ss;
			ss << "sqlite error code=" << res << ", " << sqlite3_errmsg(db);
			LOG_DEBUG() << ss.str();
			throw runtime_error(ss.str());
		}
	}
    
	namespace sqlite
	{

		struct Statement
		{
			Statement(sqlite3* db, const char* sql)
				: _db(db)
				, _stm(nullptr)
			{
				int ret = sqlite3_prepare_v2(_db, sql, -1, &_stm, NULL);
                throwIfError(ret, _db);
			}

			void bind(int col, int val)
			{
				int ret = sqlite3_bind_int(_stm, col, val);
                throwIfError(ret, _db);
			}

			void bind(int col, KeyType val)
			{
				int ret = sqlite3_bind_int(_stm, col, static_cast<int>(val));
                throwIfError(ret, _db);
			}

            void bind(int col, TxDescription::Status val)
            {
                int ret = sqlite3_bind_int(_stm, col, static_cast<int>(val));
                throwIfError(ret, _db);
            }

			void bind(int col, uint64_t val)
			{
				int ret = sqlite3_bind_int64(_stm, col, val);
                throwIfError(ret, _db);
			}

            void bind(int col, const Uuid& id)
            {
                bind(col, id.data(), id.size());
            }

            void bind(int col, const boost::optional<Uuid>& id)
            {
                if (id.is_initialized())
                {
                    bind(col, *id);
                }
                else
                {
                    bind(col, nullptr, 0);
                }
            }

            void bind(int col, const ByteBuffer& m)
            {
                int ret = sqlite3_bind_blob(_stm, col, m.data(), m.size(), NULL);
                throwIfError(ret, _db);
            }

			void bind(int col, const void* blob, int size)
			{
				int ret = sqlite3_bind_blob(_stm, col, blob, size, NULL);
                throwIfError(ret, _db);
			}

			void bind(int col, const char* val)
			{
				int ret = sqlite3_bind_text(_stm, col, val, -1, NULL);
                throwIfError(ret, _db);
			}

			bool step()
			{
				int ret = sqlite3_step(_stm);
                switch (ret)
                {
                case SQLITE_ROW: return true;   // has another row ready continue
                case SQLITE_DONE: return false; // has finished executing stop;
                default:
                    throwIfError(ret, _db);
                    return false; // and stop
                }
			}

			void get(int col, uint64_t& val)
			{
				val = sqlite3_column_int64(_stm, col);
			}

			void get(int col, int& val)
			{
				val = sqlite3_column_int(_stm, col);
			}

			void get(int col, Coin::Status& status)
			{
				status = static_cast<Coin::Status>(sqlite3_column_int(_stm, col));
			}

            void get(int col, TxDescription::Status& status)
            {
                status = static_cast<TxDescription::Status>(sqlite3_column_int(_stm, col));
            }

			void get(int col, bool& val)
			{
				val = sqlite3_column_int(_stm, col) == 0 ? false : true;
			}

            void get(int col, Uuid& id)
            {
                int size = 0;
                get(col, static_cast<void*>(id.data()), size);
                assert(size == id.size());
            }

            void get(int col, boost::optional<Uuid>& id)
            {
                int size = sqlite3_column_bytes(_stm, col);
                if (size > 0)
                {
                    size = sqlite3_column_bytes(_stm, col);
                    const void* data = sqlite3_column_blob(_stm, col);

                    if (data)
                    {
                        id = Uuid{};
                        memcpy(id->data(), data, size);
                    }
                }
            }

            void get(int col, ByteBuffer& b)
            {
                int size = sqlite3_column_bytes(_stm, col);
                const void* data = sqlite3_column_blob(_stm, col);

                if (data)
                {
                    b.clear();
                    b.resize(size);
                    memcpy(&b[0], data, size);
                }
            }

			void get(int col, void* blob, int& size)
			{
				size = sqlite3_column_bytes(_stm, col);
				const void* data = sqlite3_column_blob(_stm, col);

				if (data) memcpy(blob, data, size);
			}

			void get(int col, KeyType& type)
			{
				type = static_cast<KeyType>(sqlite3_column_int(_stm, col));
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
				if (!_commited && !_rollbacked)
					rollback();
			}

			void begin()
			{
				int ret = sqlite3_exec(_db, "BEGIN;", NULL, NULL, NULL);
				throwIfError(ret, _db);
			}

			bool commit()
			{
				int ret = sqlite3_exec(_db, "COMMIT;", NULL, NULL, NULL);

				_commited = (ret == SQLITE_OK);
                return _commited;
			}

			void rollback()
			{
				int ret = sqlite3_exec(_db, "ROLLBACK;", NULL, NULL, NULL);
				throwIfError(ret, _db);

				_rollbacked = true;
			}
		private:
			sqlite3 * _db;
			bool _commited;
			bool _rollbacked;
		};
	}

    namespace
    {
        const char* WalletSeed = "WalletSeed";
        const int BusyTimeoutMs = 1000;
    }


	Coin::Coin(const Amount& amount, Status status, const Height& height, const Height& maturity, KeyType keyType)
		: m_id{ 0 }
		, m_amount{ amount }
		, m_status{ status }
		, m_height{ height }
		, m_maturity{ maturity }
		, m_key_type{ keyType }
	{

	}

	Coin::Coin()
        : Coin(0, Coin::Unspent, 0, MaxHeight, KeyType::Regular)
	{

	}

	bool Keychain::isInitialized(const string& path)
	{
		return boost::filesystem::exists(path);
	}

	IKeyChain::Ptr Keychain::init(const string& path, const string& password, const ECC::NoLeak<ECC::uintBig>& secretKey)
	{
		if (!boost::filesystem::exists(path))
		{
			auto keychain = make_shared<Keychain>(secretKey);

			{
				int ret = sqlite3_open_v2(path.c_str(), &keychain->_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_CREATE, NULL);
				throwIfError(ret, keychain->_db);				
			}

			{
				int ret = sqlite3_key(keychain->_db, password.c_str(), password.size());
				throwIfError(ret, keychain->_db);
			}

			{
				const char* req = "CREATE TABLE " STORAGE_NAME " (" ENUM_ALL_STORAGE_FIELDS(LIST_WITH_TYPES, COMMA,) ");";
				int ret = sqlite3_exec(keychain->_db, req, NULL, NULL, NULL);
				throwIfError(ret, keychain->_db);
			}

			{
				const char* req = "CREATE TABLE " VARIABLES_NAME " (" ENUM_VARIABLES_FIELDS(LIST_WITH_TYPES, COMMA,) ");";
				int ret = sqlite3_exec(keychain->_db, req, NULL, NULL, NULL);
				throwIfError(ret, keychain->_db);
			}

            {
                const char* req = "CREATE TABLE " HISTORY_NAME " (" ENUM_HISTORY_FIELDS(LIST_WITH_TYPES, COMMA,) ") WITHOUT ROWID;";
                int ret = sqlite3_exec(keychain->_db, req, NULL, NULL, NULL);
                throwIfError(ret, keychain->_db);
            }
			{
				keychain->setVar(WalletSeed, secretKey.V);
			}

			return static_pointer_cast<IKeyChain>(keychain);
		}

		LOG_ERROR() << path << " already exists.";

		return Ptr();
	}

	IKeyChain::Ptr Keychain::open(const string& path, const string& password)
	{
		if (boost::filesystem::exists(path))
		{
			ECC::NoLeak<ECC::uintBig> seed;
			seed.V = ECC::Zero;
			auto keychain = make_shared<Keychain>(seed);

			{
				int ret = sqlite3_open_v2(path.c_str(), &keychain->_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX, NULL);
				throwIfError(ret, keychain->_db);
			}

			{
				int ret = sqlite3_key(keychain->_db, password.c_str(), password.size());
				throwIfError(ret, keychain->_db);
			}
			{
				int ret = sqlite3_busy_timeout(keychain->_db, BusyTimeoutMs);
				throwIfError(ret, keychain->_db);
			}
			{
				const char* req = "SELECT name FROM sqlite_master WHERE type='table' AND name='" STORAGE_NAME "';";
				int ret = sqlite3_exec(keychain->_db, req, NULL, NULL, NULL);
				if (ret != SQLITE_OK)
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

            {
                const char* req = "SELECT " HISTORY_FIELDS " FROM " HISTORY_NAME ";";
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

			return static_pointer_cast<IKeyChain>(keychain);
		}

		LOG_ERROR() << path << " not found, please init the wallet before.";

		return Ptr();
	}

	Keychain::Keychain(const ECC::NoLeak<ECC::uintBig>& secretKey)
		: _db(nullptr)
	{
		m_kdf.m_Secret = secretKey;
	}

	Keychain::~Keychain()
	{
		if (_db)
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
		assert(coin.m_key_type != KeyType::Regular || coin.m_id > 0);
		ECC::Scalar::Native key;
		// For coinbase and free commitments we generate key as function of (height and type), for regular coins we add id, to solve collisions
		DeriveKey(key, m_kdf, coin.m_height, coin.m_key_type, (coin.m_key_type == KeyType::Regular) ? coin.m_id : 0);
		return key;
	}

	vector<beam::Coin> Keychain::getCoins(const ECC::Amount& amount, bool lock)
	{
		vector<beam::Coin> coins;

		ECC::Amount sum = 0;

		{
			const char* req = "SELECT " STORAGE_FIELDS " FROM " STORAGE_NAME " WHERE status=?1 ORDER BY amount ASC;";
			sqlite::Statement stm(_db, req);
			stm.bind(1, Coin::Unspent);

			Block::SystemState::ID stateID = {};
			getSystemStateID(stateID);

			while (true)
			{
				if (sum >= amount) break;

				if (stm.step())
				{
					beam::Coin coin;

					ENUM_ALL_STORAGE_FIELDS(STM_GET_LIST, NOSEP, coin);

					if (coin.m_maturity <= stateID.m_Height)
					{
						if (lock)
						{
							coin.m_status = beam::Coin::Locked;
						}

						coins.push_back(coin);
						sum += coin.m_amount;
					}
				}
				else break;
			}
		}

		if (sum < amount)
		{
			coins.clear();
		}
		else if (lock)
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

        storeImpl(coin);

		trans.commit();
	}

	void Keychain::store(vector<beam::Coin>& coins)
	{
		if (coins.empty()) return;

		sqlite::Transaction trans(_db);
		for (auto& coin : coins)
		{
            storeImpl(coin);
		}

		trans.commit();
	}

    void Keychain::storeImpl(Coin& coin)
    {
        if (coin.m_key_type == KeyType::Coinbase
            || coin.m_key_type == KeyType::Comission)
        {
            const char* req = "SELECT " STORAGE_FIELDS " FROM " STORAGE_NAME " WHERE height=?1 AND key_type=?2;";
            sqlite::Statement stm(_db, req);
            stm.bind(1, coin.m_height);
            stm.bind(2, coin.m_key_type);
            if (stm.step()) //has row
            {
                return; // skip existing
            }
        }

        const char* req = "INSERT INTO " STORAGE_NAME " (" ENUM_STORAGE_FIELDS(LIST, COMMA, ) ") VALUES(" ENUM_STORAGE_FIELDS(BIND_LIST, COMMA, ) ");";
        sqlite::Statement stm(_db, req);

        ENUM_STORAGE_FIELDS(STM_BIND_LIST, NOSEP, coin);

        stm.step();

        coin.m_id = getLastID(_db);
    }

	void Keychain::update(const vector<beam::Coin>& coins)
	{
		if (coins.size())
		{
			sqlite::Transaction trans(_db);

			for (const auto& coin : coins)
			{
				const char* req = "UPDATE " STORAGE_NAME " SET " ENUM_STORAGE_FIELDS(SET_LIST, COMMA, ) " WHERE id=?1;";
				sqlite::Statement stm(_db, req);

				ENUM_ALL_STORAGE_FIELDS(STM_BIND_LIST, NOSEP, coin);

				stm.step();
			}

			trans.commit();
		}
	}

	void Keychain::remove(const vector<beam::Coin>& coins)
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

	void Keychain::visit(function<bool(const beam::Coin& coin)> func)
	{
		const char* req = "SELECT " STORAGE_FIELDS " FROM " STORAGE_NAME ";";
		sqlite::Statement stm(_db, req);

		while (stm.step())
		{
			Coin coin;

			ENUM_ALL_STORAGE_FIELDS(STM_GET_LIST, NOSEP, coin);

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
		Block::SystemState::ID id = {};
		if (getSystemStateID(id))
		{
			return id.m_Height;
		}
		return 0;
	}

    vector<TxDescription> Keychain::getTxHistory(uint64_t start, int count)
    {
        vector<TxDescription> res;
        const char* req = "SELECT * FROM " HISTORY_NAME " ORDER BY createTime DESC LIMIT ?1 OFFSET ?2 ;";

        sqlite::Statement stm(_db, req);
        stm.bind(1, count);
        stm.bind(2, start);

        while (stm.step())
        {
            auto& tx = res.emplace_back(TxDescription{});
            ENUM_HISTORY_FIELDS(STM_GET_LIST, NOSEP, tx);
        }
        return res;
    }

    boost::optional<TxDescription> Keychain::getTx(const Uuid& txId)
    {
        const char* req = "SELECT * FROM " HISTORY_NAME " WHERE txId=?1 ;";
        sqlite::Statement stm(_db, req);
        stm.bind(1, txId);

        if (stm.step())
        {
            TxDescription tx;
            ENUM_HISTORY_FIELDS(STM_GET_LIST, NOSEP, tx);
            return tx;
        }

        return boost::optional<TxDescription>{};
    }

    void Keychain::saveTx(const TxDescription& p)
    {
        const char* selectReq = "SELECT * FROM " HISTORY_NAME " WHERE txId=?1;";
        sqlite::Statement stm2(_db, selectReq);
        stm2.bind(1, p.m_txId);

        if (stm2.step())
        {
            const char* updateReq = "UPDATE " HISTORY_NAME " SET modifyTime=?2, status=?3, fsmState=?4 WHERE txId=?1;";
            sqlite::Statement stm(_db, updateReq);

            stm.bind(1, p.m_txId);
            stm.bind(2, p.m_modifyTime);
            stm.bind(3, p.m_status);
            stm.bind(4, p.m_fsmState);
            stm.step();
        }
        else
        {
            const char* insertReq = "INSERT INTO " HISTORY_NAME " (" ENUM_HISTORY_FIELDS(LIST, COMMA,) ") VALUES(" ENUM_HISTORY_FIELDS(BIND_LIST, COMMA,) ");";
            sqlite::Statement stm(_db, insertReq);
            ENUM_HISTORY_FIELDS(STM_BIND_LIST, NOSEP, p);
            stm.step();
        }
    }

    void Keychain::deleteTx(const Uuid& txId)
    {
        const char* req = "DELETE FROM " HISTORY_NAME " WHERE txId=?1;";
        sqlite::Statement stm(_db, req);

        stm.bind(1, txId);

        stm.step();
    }

    void Keychain::rollbackTx(const Uuid& txId)
    {
        sqlite::Transaction trans(_db);

        {
            const char* req = "UPDATE " STORAGE_NAME " SET status=?3 WHERE spentTxId=?1 AND status=?2;";
            sqlite::Statement stm(_db, req);
            stm.bind(1, txId);
            stm.bind(2, Coin::Locked);
            stm.bind(3, Coin::Unspent);
            stm.step();
        }
        {
            const char* req = "DELETE FROM " STORAGE_NAME " WHERE createTxId=?1;";
            sqlite::Statement stm(_db, req);
            stm.bind(1, txId);
            stm.step();
        }
        trans.commit();
    }
}
