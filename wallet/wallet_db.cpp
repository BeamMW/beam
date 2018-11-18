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

#include "wallet_db.h"
#include "wallet_transaction.h"
#include "utility/logger.h"
#include "sqlite/sqlite3.h"
#include <sstream>
#include <boost/functional/hash.hpp>
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
// status      - spent/unspent/unconfirmed/locked/draft
// maturity    - height where we can spend the coin

#define ENUM_STORAGE_ID(each, sep, obj) \
    each(1, id, sep, INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, obj)

#define ENUM_STORAGE_FIELDS(each, sep, obj) \
    each(2, amount,        sep, INTEGER NOT NULL, obj) \
    each(3, status,        sep, INTEGER NOT NULL, obj) \
    each(4, createHeight,  sep, INTEGER NOT NULL, obj) \
    each(5, maturity,      sep, INTEGER NOT NULL, obj) \
    each(6, key_type,      sep, INTEGER NOT NULL, obj) \
    each(7, keyIndex,      sep, INTEGER NOT NULL, obj) \
    each(8, confirmHeight, sep, INTEGER, obj) \
    each(9, confirmHash,   sep, BLOB, obj) \
    each(10, createTxId,    sep, BLOB, obj) \
    each(11, spentTxId,    sep, BLOB, obj) \
    each(12, lockedHeight,    , BLOB, obj)            // last item without separator// last item without separator

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
#define PEERS_NAME "peers"
#define ADDRESSES_NAME "addresses"
#define TX_PARAMS_NAME "txparams"

#define ENUM_VARIABLES_FIELDS(each, sep, obj) \
    each(1, name, sep, TEXT UNIQUE, obj) \
    each(2, value,   , BLOB, obj)

#define VARIABLES_FIELDS ENUM_VARIABLES_FIELDS(LIST, COMMA, )

#define ENUM_HISTORY_FIELDS(each, sep, obj) \
    each(1, txId,       sep, BLOB NOT NULL PRIMARY KEY, obj) \
    each(2, amount,     sep, INTEGER NOT NULL, obj) \
    each(3, fee,        sep, INTEGER NOT NULL, obj) \
    each(4, minHeight,  sep, INTEGER NOT NULL, obj) \
    each(5, peerId,     sep, BLOB NOT NULL, obj) \
    each(6, myId,       sep, BLOB NOT NULL, obj) \
    each(7, message,    sep, BLOB, obj) \
    each(8, createTime, sep, INTEGER NOT NULL, obj) \
    each(9, modifyTime, sep, INTEGER, obj) \
    each(10, sender,    sep, INTEGER NOT NULL, obj) \
    each(11, status,    sep, INTEGER NOT NULL, obj) \
    each(12, fsmState,  sep, BLOB, obj) \
    each(13, change,       , INTEGER NOT NULL, obj)
#define HISTORY_FIELDS ENUM_HISTORY_FIELDS(LIST, COMMA, )

#define ENUM_PEER_FIELDS(each, sep, obj) \
    each(1, walletID,    sep, BLOB NOT NULL PRIMARY KEY, obj) \
    each(2, address,     sep, TEXT NOT NULL, obj) \
    each(3, label,          , TEXT NOT NULL , obj)

#define PEER_FIELDS ENUM_PEER_FIELDS(LIST, COMMA, )


#define ENUM_ADDRESS_FIELDS(each, sep, obj) \
    each(1, walletID ,      sep, BLOB NOT NULL PRIMARY KEY, obj) \
    each(2, label,          sep, TEXT NOT NULL, obj) \
    each(3, category,       sep, TEXT, obj) \
    each(4, createTime,     sep, INTEGER, obj) \
    each(5, duration,       sep, INTEGER, obj) \
    each(6, own,               , INTEGER NOT NULL, obj)

#define ADDRESS_FIELDS ENUM_ADDRESS_FIELDS(LIST, COMMA, )

#define ENUM_TX_PARAMS_FIELDS(each, sep, obj) \
    each(1, txID ,          sep, BLOB NOT NULL , obj) \
    each(2, paramID,        sep, INTEGER NOT NULL , obj) \
    each(3, value,             , BLOB, obj) \

#define TX_PARAMS_FIELDS ENUM_TX_PARAMS_FIELDS(LIST, COMMA, )

#define TblStates			"States"
#define TblStates_Height	"Height"
#define TblStates_Hdr		"State"

namespace std
{
    template<>
    struct hash<pair<beam::Amount, beam::Amount>>
    {
        typedef pair<beam::Amount, beam::Amount> argument_type;
        typedef std::size_t result_type;

        result_type operator()(const argument_type& a) const noexcept
        {
            return boost::hash<argument_type>()(a);
        }
    };
}

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


        void enterKey(sqlite3 * db, const SecString& password)
        {
            if (password.size() > numeric_limits<int>::max())
            {
                throwIfError(SQLITE_TOOBIG, db);
            }
            int ret = sqlite3_key(db, password.data(), static_cast<int>(password.size()));
            throwIfError(ret, db);
        }

        struct CoinSelector2
        {
            using Result = pair<Amount, vector<Coin>>;
            CoinSelector2(const vector<Coin>& coins)
                : m_coins{coins}
            {

            }

            Result select(Amount amount)
            {
                m_amount = amount;
                m_result.first = 0;
                m_result.second.clear();

                GenerateCombinations();
                FindBestResult();
                SelectCoins();
                return m_result;
            }

        private:

            void GenerateCombinations()
            {
                for (auto coin = m_coins.begin(); coin != m_coins.end(); ++coin)
                {
                    if (coin->m_amount > m_amount)
                    {
                        m_Combinations[coin->m_amount] = coin->m_amount;
                        continue;
                    }

                    if (coin->m_amount == m_amount)
                    {
                        m_Combinations[coin->m_amount] = coin->m_amount;
                        break;
                    }

                    vector<Amount> newCombinations;

                    {
                        auto it = m_Combinations.find(coin->m_amount);
                        if (it == m_Combinations.end())
                        {
                            newCombinations.push_back(coin->m_amount);
                        }
                    }

                    for (const auto& sum : m_Combinations)
                    {
                        if (sum.first < m_amount)
                            newCombinations.push_back(sum.first + coin->m_amount);
                    }

                    for (const auto& sum : newCombinations)
                    {
                        auto it = m_Combinations.find(sum);
                        if (it == m_Combinations.end())
                        {
                            m_Combinations[sum] = coin->m_amount;
                        }
                    }
                }
            }

            void FindBestResult()
            {
                auto it = m_Combinations.lower_bound(m_amount);

                if (it == m_Combinations.end())
                {
                    return;
                }

                m_result.first = it->first;

                for (; it != m_Combinations.end() && it->second > 0; it = m_Combinations.find(it->first - it->second))
                {
                    auto i = m_intermediateResult.find(it->second);
                    if (i == m_intermediateResult.end())
                    {
                        m_intermediateResult[it->second] = 1;
                    }
                    else
                    {
                        ++m_intermediateResult[it->second];
                    }
                }
            }

            void SelectCoins()
            {
                for (const auto& p : m_intermediateResult)
                {
                    auto it = find_if(m_coins.begin(), m_coins.end(), [amount = p.first](const Coin& c)
                    {
                        return c.m_amount == amount;
                    });

                    for (Amount i = 0; i < p.second; ++i, ++it)
                    {
                        m_result.second.push_back(*it);
                    }
                }
            }

            const vector<Coin>& m_coins;
            Result m_result;
            Amount m_amount;
            map<Amount, Amount> m_Combinations;
            map<Amount, Amount> m_intermediateResult;
        };

        struct CoinSelector
        {
            CoinSelector(const std::vector<Coin>& coins)
                : m_coins{ coins }
                , m_it{coins.begin()}
                , m_last{coins.cend()}
                , m_empty{ 0 ,{} }
            {

            }

            const pair<Amount, vector<Coin>>& select(Amount amount, Amount left)
            {
                if (left < amount || amount == 0)
                {
                    return m_empty;
                }

                if (amount == left)
                {
                    auto p = m_memory.insert({ { amount, left },{ amount, { m_it, m_last } } });
                    return p.first->second;
                }

                if (auto it = m_memory.find({ amount, left }); it != m_memory.end())
                {
                    return it->second;
                }

                Amount coinAmount = m_it->m_amount;
                Amount newLeft = left - coinAmount;

                ++m_it;
                auto res1 = select(amount, newLeft);
                auto res2 = select(amount - coinAmount, newLeft);
                --m_it;
                auto sum1 = res1.first;
                auto sum2 = res2.first + coinAmount;

                bool a = sum2 >= amount;
                bool b = sum1 >= amount;
                bool c = sum1 < sum2;

                if ((a && b && c) || (!a && b))
                {
                    auto p = m_memory.insert({ { amount, left },{ sum1, move(res1.second) } });
                    return p.first->second;
                }
                else if ((a && b && !c) || (a && !b))
                {
                    res2.second.push_back(*m_it);
                    auto p = m_memory.insert({ { amount, left },{ sum2, move(res2.second) } });
                    return p.first->second;
                }
                return m_empty;
            }
            const std::vector<Coin>& m_coins;
            std::vector<Coin>::const_iterator m_it;
            const std::vector<Coin>::const_iterator m_last;
            unordered_map<pair<Amount, Amount>, pair<Amount, vector<Coin>>> m_memory;
            pair<Amount, vector<Coin>> m_empty;
        };
    }

    namespace sqlite
    {
        struct Statement
        {
            Statement(sqlite3* db, const char* sql)
                : _db(db)
                , _stm(nullptr)
            {
                int ret = sqlite3_prepare_v2(_db, sql, -1, &_stm, nullptr);
                throwIfError(ret, _db);
            }

			void Reset()
			{
				sqlite3_reset(_stm);
			}

            void bind(int col, int val)
            {
                int ret = sqlite3_bind_int(_stm, col, val);
                throwIfError(ret, _db);
            }

            void bind(int col, Key::Type val)
            {
                int ret = sqlite3_bind_int(_stm, col, static_cast<int>(val));
                throwIfError(ret, _db);
            }

            void bind(int col, TxStatus val)
            {
                int ret = sqlite3_bind_int(_stm, col, static_cast<int>(val));
                throwIfError(ret, _db);
            }

            void bind(int col, uint64_t val)
            {
                int ret = sqlite3_bind_int64(_stm, col, val);
                throwIfError(ret, _db);
            }

            void bind(int col, const TxID& id)
            {
                bind(col, id.data(), id.size());
            }

            void bind(int col, const boost::optional<TxID>& id)
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

            void bind(int col, const ECC::Hash::Value& hash)
            {
                bind(col, hash.m_pData, hash.nBytes);
            }

            void bind(int col, const io::Address& address)
            {
                bind(col, address.u64());
            }

            void bind(int col, const ByteBuffer& m)
            {
                bind(col, m.data(), m.size());
            }

            void bind(int col, const void* blob, size_t size)
            {
                if (size > numeric_limits<int32_t>::max())// 0x7fffffff
                {
                    throwIfError(SQLITE_TOOBIG, _db);
                }
                int ret = sqlite3_bind_blob(_stm, col, blob, static_cast<int>(size), nullptr);
                throwIfError(ret, _db);
            }

			void bind(int col, const Block::SystemState::Full& s)
			{
				bind(col, &s, sizeof(s));
			}

            void bind(int col, const char* val)
            {
                int ret = sqlite3_bind_text(_stm, col, val, -1, nullptr);
                throwIfError(ret, _db);
            }

            void bind(int col, const string& val) // utf-8
            {
                int ret = sqlite3_bind_text(_stm, col, val.data(), -1, nullptr);
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

            void get(int col, TxStatus& status)
            {
                status = static_cast<TxStatus>(sqlite3_column_int(_stm, col));
            }

            void get(int col, bool& val)
            {
                val = sqlite3_column_int(_stm, col) == 0 ? false : true;
            }

            void get(int col, TxID& id)
            {
                getBlobStrict(col, static_cast<void*>(id.data()), static_cast<int>(id.size()));
            }

			void get(int col, Block::SystemState::Full& s)
			{
				// read/write as a blob, skip serialization
				getBlobStrict(col, &s, sizeof(s));
			}

            void get(int col, boost::optional<TxID>& id)
            {
                int size = sqlite3_column_bytes(_stm, col);
                if (size > 0)
                {
                    size = sqlite3_column_bytes(_stm, col);
                    const void* data = sqlite3_column_blob(_stm, col);

                    if (data)
                    {
                        id = TxID{};
                        memcpy(id->data(), data, size);
                    }
                }
            }

            void get(int col, ECC::Hash::Value& hash)
            {
                int size = sqlite3_column_bytes(_stm, col);
                if (size > 0)
                {
                    size = sqlite3_column_bytes(_stm, col);
                    const void* data = sqlite3_column_blob(_stm, col);

                    if (data)
                    {
                        assert(size == hash.nBytes);
                        memcpy(hash.m_pData, data, size);
                    }
                }
            }

            void get(int col, io::Address& address)
            {
                uint64_t t = 0;;
                get(col, t);
                address = io::Address::from_u64(t);
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

			bool getBlobSafe(int col, void* blob, int size)
			{
				if (sqlite3_column_bytes(_stm, col) != size)
					return false;

				if (size)
				{
					const void* data = sqlite3_column_blob(_stm, col);
					if (!data)
						return false;

					memcpy(blob, data, size);
				}

				return true;
			}

			void getBlobStrict(int col, void* blob, int size)
			{
				if (!getBlobSafe(col, blob, size))
					throw std::runtime_error("wdb corruption");
			}

            void get_(int col, void* blob, int& size)
            {
                size = sqlite3_column_bytes(_stm, col);
                const void* data = sqlite3_column_blob(_stm, col);

                if (data) memcpy(blob, data, size);
            }

            void get(int col, Key::Type& type)
            {
                type = static_cast<Key::Type>(sqlite3_column_int(_stm, col));
            }

            void get(int col, string& str) // utf-8
            {
                int size = sqlite3_column_bytes(_stm, col);
                const unsigned char* data = sqlite3_column_text(_stm, col);
                if (data && size)
                {
                    str.assign(reinterpret_cast<const string::value_type*>(data));
                }
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
                int ret = sqlite3_exec(_db, "BEGIN;", nullptr, nullptr, nullptr);
                throwIfError(ret, _db);
            }

            bool commit()
            {
                int ret = sqlite3_exec(_db, "COMMIT;", nullptr, nullptr, nullptr);

                _commited = (ret == SQLITE_OK);
                return _commited;
            }

            void rollback()
            {
                int ret = sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
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
        const char* Version = "Version";
        const char* SystemStateIDName = "SystemStateID";
        const char* LastUpdateTimeName = "LastUpdateTime";
        const int BusyTimeoutMs = 1000;
        const int DbVersion = 6;
    }

    Coin::Coin(const Amount& amount, Status status, const Height& createHeight, const Height& maturity, Key::Type keyType, Height confirmHeight, Height lockedHeight)
        : m_id{ 0 }
        , m_amount{ amount }
        , m_status{ status }
        , m_createHeight{ createHeight }
        , m_maturity{ maturity }
        , m_key_type{ keyType }
        , m_confirmHeight{ confirmHeight }
        , m_confirmHash(Zero)
        , m_lockedHeight{ lockedHeight }
        , m_keyIndex{0}
	{
        assert(isValid());
    }

    Coin::Coin()
        : Coin(0, Coin::Unspent, 0, MaxHeight, Key::Type::Regular, MaxHeight)
    {
        assert(isValid());
    }

    bool Coin::isReward() const
    {
        return m_key_type == Key::Type::Coinbase || m_key_type == Key::Type::Comission;
    }

    bool Coin::isValid() const
    {
        return m_createHeight <= m_maturity
            && m_maturity <= m_lockedHeight;
    }

	uint64_t IWalletDB::get_AutoIncrID()
	{
		uintBigFor<uint64_t>::Type val;

		const char* szParamName = "auto_id";

		if (getVar(szParamName, val))
			val.Inc();
		else
			val = 1U;

		setVar(szParamName, val);
		
		uint64_t res;
		val.Export(res);
		return res;
	}

    Key::IDV Coin::get_Kidv() const
    {
        // For coinbase and fee commitments we generate key as function of (height and type), for regular coins we add id, to solve collisions
        Key::IDV kidv(m_amount, m_createHeight, m_key_type, m_keyIndex);

        switch (m_key_type)
        {
        case Key::Type::Coinbase:
        case Key::Type::Comission:
            kidv.m_IdxSecondary = 0;
            break;

        default: // suppress warning
            break;
        }

        return kidv;
    }

    Coin Coin::fromKidv(const Key::IDV& kidv)
    {
        Coin c(kidv.m_Value, Coin::Unconfirmed, kidv.m_Idx, MaxHeight, kidv.m_Type);
        c.m_keyIndex = kidv.m_IdxSecondary;
        assert(c.isValid());
        return c;
    }

    bool WalletDB::isInitialized(const string& path)
    {
#ifdef WIN32
        return boost::filesystem::exists(Utf8toUtf16(path.c_str()));
#else
        return boost::filesystem::exists(path);
#endif
    }

    IWalletDB::Ptr WalletDB::init(const string& path, const SecString& password, const ECC::NoLeak<ECC::uintBig>& secretKey)
    {
        if (!isInitialized(path))
        {
            auto walletDB = make_shared<WalletDB>(secretKey);

            {
                int ret = sqlite3_open_v2(path.c_str(), &walletDB->_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_CREATE, nullptr);
                throwIfError(ret, walletDB->_db);
            }

            enterKey(walletDB->_db, password);

            {
                const char* req = "CREATE TABLE " STORAGE_NAME " (" ENUM_ALL_STORAGE_FIELDS(LIST_WITH_TYPES, COMMA,) ");"
                                  "CREATE INDEX ConfirmIndex ON " STORAGE_NAME"(confirmHeight);"
                                  "CREATE INDEX SpentIndex ON " STORAGE_NAME"(lockedHeight);";
                int ret = sqlite3_exec(walletDB->_db, req, nullptr, nullptr, nullptr);
                throwIfError(ret, walletDB->_db);
            }

            {
                const char* req = "CREATE TABLE " VARIABLES_NAME " (" ENUM_VARIABLES_FIELDS(LIST_WITH_TYPES, COMMA,) ");";
                int ret = sqlite3_exec(walletDB->_db, req, nullptr, nullptr, nullptr);
                throwIfError(ret, walletDB->_db);
            }

            {
                const char* req = "CREATE TABLE " PEERS_NAME " (" ENUM_PEER_FIELDS(LIST_WITH_TYPES, COMMA, ) ") WITHOUT ROWID;";
                int ret = sqlite3_exec(walletDB->_db, req, nullptr, nullptr, nullptr);
                throwIfError(ret, walletDB->_db);
            }

            {
                const char* req = "CREATE TABLE " ADDRESSES_NAME " (" ENUM_ADDRESS_FIELDS(LIST_WITH_TYPES, COMMA, ) ") WITHOUT ROWID;";
                int ret = sqlite3_exec(walletDB->_db, req, nullptr, nullptr, nullptr);
                throwIfError(ret, walletDB->_db);
            }

            {
                const char* req = "CREATE TABLE " TX_PARAMS_NAME " (" ENUM_TX_PARAMS_FIELDS(LIST_WITH_TYPES, COMMA, ) ", PRIMARY KEY (txID, paramID)) WITHOUT ROWID;";
                int ret = sqlite3_exec(walletDB->_db, req, nullptr, nullptr, nullptr);
                throwIfError(ret, walletDB->_db);
            }

			{
				const char* req = "CREATE TABLE [" TblStates "] ("
					"[" TblStates_Height	"] INTEGER NOT NULL PRIMARY KEY,"
					"[" TblStates_Hdr		"] BLOB NOT NULL)";
				int ret = sqlite3_exec(walletDB->_db, req, nullptr, nullptr, nullptr);
				throwIfError(ret, walletDB->_db);
			}

            {
                walletDB->setVar(WalletSeed, secretKey.V);
                walletDB->setVar(Version, DbVersion);
            }

            return static_pointer_cast<IWalletDB>(walletDB);
        }

        LOG_ERROR() << path << " already exists.";

        return Ptr();
    }

    IWalletDB::Ptr WalletDB::open(const string& path, const SecString& password)
    {
        try
        {
            if (isInitialized(path))
            {
				std::shared_ptr<WalletDB> walletDB(new WalletDB);

                {
                    int ret = sqlite3_open_v2(path.c_str(), &walletDB->_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX, nullptr);
                    throwIfError(ret, walletDB->_db);
                }

                enterKey(walletDB->_db, password);
                {
                    int ret = sqlite3_busy_timeout(walletDB->_db, BusyTimeoutMs);
                    throwIfError(ret, walletDB->_db);
                }
                {
                    int version = 0;
                    if (!walletDB->getVar(Version, version) || version > DbVersion)
                    {
                        LOG_DEBUG() << "Invalid DB version: " << version << ". Expected: " << DbVersion;
                        return Ptr();
                    }
                }
                {
                    const char* req = "SELECT name FROM sqlite_master WHERE type='table' AND name='" STORAGE_NAME "';";
                    int ret = sqlite3_exec(walletDB->_db, req, nullptr, nullptr, nullptr);
                    if (ret != SQLITE_OK)
                    {
                        LOG_ERROR() << "Invalid DB or wrong password :(";
                        return Ptr();
                    }
                }

                {
                    const char* req = "SELECT " STORAGE_FIELDS " FROM " STORAGE_NAME ";";
                    int ret = sqlite3_exec(walletDB->_db, req, nullptr, nullptr, nullptr);
                    if (ret != SQLITE_OK)
                    {
                        LOG_ERROR() << "Invalid DB format :(";
                        return Ptr();
                    }
                }

                {
                    const char* req = "SELECT " VARIABLES_FIELDS " FROM " VARIABLES_NAME ";";
                    int ret = sqlite3_exec(walletDB->_db, req, nullptr, nullptr, nullptr);
                    if (ret != SQLITE_OK)
                    {
                        LOG_ERROR() << "Invalid DB format :(";
                        return Ptr();
                    }
                }

                {
                    const char* req = "CREATE TABLE IF NOT EXISTS " TX_PARAMS_NAME " (" ENUM_TX_PARAMS_FIELDS(LIST_WITH_TYPES, COMMA, ) ", PRIMARY KEY (txID, paramID)) WITHOUT ROWID;";
                    int ret = sqlite3_exec(walletDB->_db, req, NULL, NULL, NULL);
                    throwIfError(ret, walletDB->_db);
                }

				std::shared_ptr<ECC::HKdf> pKdf(new ECC::HKdf);

                if (!walletDB->getVar(WalletSeed, pKdf->m_Secret.V))
                {
					assert(false && "there is no seed for walletDB");
					//pKdf->m_Secret.V = Zero;
					return Ptr();
				}

				walletDB->m_pKdf = pKdf;

                return static_pointer_cast<IWalletDB>(walletDB);
            }

            LOG_ERROR() << path << " not found, please init the wallet before.";
        }
        catch (const runtime_error&)
        {

        }

        return Ptr();
    }

	WalletDB::WalletDB()
		: _db(nullptr)
	{
	}

    WalletDB::WalletDB(const ECC::NoLeak<ECC::uintBig>& secretKey)
        : _db(nullptr)
    {
		std::shared_ptr<ECC::HKdf> pKdf(new ECC::HKdf);
		pKdf->m_Secret = secretKey;
		m_pKdf = pKdf;
	}

    WalletDB::~WalletDB()
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

	Key::IKdf::Ptr WalletDB::get_Kdf() const
	{
		return m_pKdf;
	}

    ECC::Scalar::Native WalletDB::calcKey(const beam::Coin& coin) const
    {
		assert(coin.m_key_type != Key::Type::Regular || coin.m_keyIndex > 0);

		ECC::Scalar::Native key;
		m_pKdf->DeriveKey(key, coin.get_Kidv());
        return key;
    }

    void WalletDB::get_IdentityKey(ECC::Scalar::Native& sk) const
    {
		m_pKdf->DeriveKey(sk, Key::ID(0, Key::Type::Identity));
    }

    vector<beam::Coin> WalletDB::selectCoins(const Amount& amount, bool lock)
    {
        vector<beam::Coin> coins;
        Block::SystemState::ID stateID = {};
        getSystemStateID(stateID);
        {
            sqlite::Statement stm(_db, "SELECT SUM(amount)" STORAGE_FIELDS " FROM " STORAGE_NAME " WHERE status=?1 AND maturity<=?2 ;");
            stm.bind(1, Coin::Unspent);
            stm.bind(2, stateID.m_Height);
            Amount avalableAmount = 0;
            if (stm.step())
            {
                stm.get(0, avalableAmount);
            }
            if (avalableAmount < amount)
            {
                return coins;
            }
        }
        Amount sum = 0;
        Coin coin2;
        {
            // get one coin >= amount
            sqlite::Statement stm(_db, "SELECT " STORAGE_FIELDS " FROM " STORAGE_NAME " WHERE status=?1 AND maturity<=?2 AND amount>=?3 ORDER BY amount ASC LIMIT 1;");
            stm.bind(1, Coin::Unspent);
            stm.bind(2, stateID.m_Height);
            stm.bind(3, amount);
            if (stm.step())
            {
                ENUM_ALL_STORAGE_FIELDS(STM_GET_LIST, NOSEP, coin2);
                sum = coin2.m_amount;
            }
        }
        if (sum == amount)
        {
            coins.push_back(coin2);
        }
        else
        {
            // select all coins less than needed amount in sorted order
            sqlite::Statement stm(_db, "SELECT " STORAGE_FIELDS " FROM " STORAGE_NAME " WHERE status=?1 AND maturity<=?2 AND amount<?3 ORDER BY amount DESC;");
            stm.bind(1, Coin::Unspent);
            stm.bind(2, stateID.m_Height);
            stm.bind(3, amount);
            vector<Coin> candidats;
            Amount smallSum = 0;
            while (stm.step())
            {
                auto& coin = candidats.emplace_back();
                ENUM_ALL_STORAGE_FIELDS(STM_GET_LIST, NOSEP, coin);
                smallSum += coin.m_amount;
            }
            if (smallSum == amount)
            {
                coins.swap(candidats);
            }
            else if (smallSum > amount)
            {
                CoinSelector2 s{ candidats };
                auto t = s.select(amount);

                if (sum > amount && sum <= t.first)
                {
                    // prefer one coin instead on many
                    coins.push_back(coin2);
                }
                else
                {
                    coins = t.second;
                }
            }
            else if (sum > amount)
            {
                coins.push_back(coin2);
            }
        }

        if (lock)
        {
            sqlite::Transaction trans(_db);

            for (auto& coin : coins)
            {
                coin.m_status = Coin::Locked;
                const char* req = "UPDATE " STORAGE_NAME " SET status=?2, lockedHeight=?3 WHERE id=?1;";
                sqlite::Statement stm(_db, req);

                stm.bind(1, coin.m_id);
                stm.bind(2, coin.m_status);
                stm.bind(3, stateID.m_Height);

                stm.step();
            }

            trans.commit();

            notifyCoinsChanged();
        }
        std::sort(coins.begin(), coins.end(), [](const Coin& lhs, const Coin& rhs) {return lhs.m_amount < rhs.m_amount; });
        return coins;
    }

    std::vector<beam::Coin> WalletDB::getCoinsCreatedByTx(const TxID& txId)
    {
        // select all coins for TxID
        sqlite::Statement stm(_db, "SELECT " STORAGE_FIELDS " FROM " STORAGE_NAME " WHERE createTxID=?1 ORDER BY amount DESC;");
        stm.bind(1, txId);

        vector<Coin> coins;

        while (stm.step())
        {
            auto& coin = coins.emplace_back();
            ENUM_ALL_STORAGE_FIELDS(STM_GET_LIST, NOSEP, coin);
        }

        return coins;
    }

    void WalletDB::store(beam::Coin& coin)
    {
        sqlite::Transaction trans(_db);

        storeImpl(coin);

        trans.commit();
    }

    void WalletDB::store(vector<beam::Coin>& coins)
    {
        if (coins.empty()) return;

        sqlite::Transaction trans(_db);
        for (auto& coin : coins)
        {
            storeImpl(coin);
        }

        trans.commit();
    }

    void WalletDB::storeImpl(Coin& coin)
    {
        assert(coin.m_amount > 0 && coin.isValid());
        if (coin.isReward())
        {
            const char* req = "SELECT " STORAGE_FIELDS " FROM " STORAGE_NAME " WHERE createHeight=?1 AND key_type=?2;";
            sqlite::Statement stm(_db, req);
            stm.bind(1, coin.m_createHeight);
            stm.bind(2, coin.m_key_type);
            if (stm.step()) //has row
            {
                return; // skip existing
            }
        }
        else if (coin.m_keyIndex == 0)
        {
            coin.m_keyIndex = getLastID(_db) + 1;
        }

        {
            sqlite::Statement stm(_db, "SELECT " STORAGE_FIELDS " FROM " STORAGE_NAME " WHERE createHeight=?1 AND key_type=?2 AND keyIndex=?3; ");
            stm.bind(1, coin.m_createHeight);
            stm.bind(2, coin.m_key_type);
            stm.bind(3, coin.m_keyIndex);
            if (stm.step())
            {
                Amount amount = coin.m_amount;
                ENUM_ALL_STORAGE_FIELDS(STM_GET_LIST, NOSEP, coin);
                if (amount != coin.m_amount)
                {
                    LOG_WARNING() << "Attempt to store invalid UTXO";
                }
                return;
            }
        }

        const char* req = "INSERT INTO " STORAGE_NAME " (" ENUM_STORAGE_FIELDS(LIST, COMMA, ) ") VALUES(" ENUM_STORAGE_FIELDS(BIND_LIST, COMMA, ) ");";
        sqlite::Statement stm(_db, req);

        ENUM_STORAGE_FIELDS(STM_BIND_LIST, NOSEP, coin);

        stm.step();

        coin.m_id = getLastID(_db);

        notifyCoinsChanged();
    }

    void WalletDB::update(const beam::Coin& coin)
    {
        assert(coin.m_amount > 0 && coin.m_id > 0 && coin.isValid());
        sqlite::Transaction trans(_db);

        {
            const char* req = "UPDATE " STORAGE_NAME " SET " ENUM_STORAGE_FIELDS(SET_LIST, COMMA, ) " WHERE id=?1;";
            sqlite::Statement stm(_db, req);

            ENUM_ALL_STORAGE_FIELDS(STM_BIND_LIST, NOSEP, coin);

            stm.step();
        }

        trans.commit();

        notifyCoinsChanged();
    }

    void WalletDB::update(const vector<beam::Coin>& coins)
    {
        if (coins.size())
        {
            sqlite::Transaction trans(_db);

            for (const auto& coin : coins)
            {
                assert(coin.m_amount > 0 && coin.m_id > 0 && coin.isValid());
                const char* req = "UPDATE " STORAGE_NAME " SET " ENUM_STORAGE_FIELDS(SET_LIST, COMMA, ) " WHERE id=?1;";
                sqlite::Statement stm(_db, req);

                ENUM_ALL_STORAGE_FIELDS(STM_BIND_LIST, NOSEP, coin);

                stm.step();
            }

            trans.commit();
            notifyCoinsChanged();
        }
    }

    void WalletDB::remove(const vector<beam::Coin>& coins)
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

            notifyCoinsChanged();
        }
    }

    void WalletDB::remove(const beam::Coin& coin)
    {
        sqlite::Transaction trans(_db);

        const char* req = "DELETE FROM " STORAGE_NAME " WHERE id=?1;";
        sqlite::Statement stm(_db, req);

        stm.bind(1, coin.m_id);

        stm.step();
        trans.commit();

        notifyCoinsChanged();
    }

    void WalletDB::clear()
    {
        {
            sqlite::Statement stm(_db, "DELETE FROM " STORAGE_NAME ";");
            stm.step();
            notifyCoinsChanged();
        }

        {
            sqlite::Statement stm(_db, "DELETE FROM " TX_PARAMS_NAME ";");
            stm.step();
            notifyTransactionChanged(ChangeAction::Reset, {});
        }
    }

    void WalletDB::visit(function<bool(const beam::Coin& coin)> func)
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

    void WalletDB::setVarRaw(const char* name, const void* data, size_t size)
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

    bool WalletDB::getVarRaw(const char* name, void* data, int size) const
    {
        const char* req = "SELECT value FROM " VARIABLES_NAME " WHERE name=?1;";

        sqlite::Statement stm(_db, req);
        stm.bind(1, name);

        return
			stm.step() &&
			stm.getBlobSafe(0, data, size);
    }

    bool WalletDB::getBlob(const char* name, ByteBuffer& var) const
    {
        const char* req = "SELECT value FROM " VARIABLES_NAME " WHERE name=?1;";

        sqlite::Statement stm(_db, req);
        stm.bind(1, name);
        if (stm.step())
        {
            stm.get(0, var);
            return true;
        }
        return false;
    }

    Timestamp WalletDB::getLastUpdateTime() const
    {
        Timestamp timestamp = {};
        if (getVar(LastUpdateTimeName, timestamp))
        {
            return timestamp;
        }
        return 0;
    }

    void WalletDB::setSystemStateID(const Block::SystemState::ID& stateID)
    {
        setVar(SystemStateIDName, stateID);
        setVar(LastUpdateTimeName, getTimestamp());
        notifySystemStateChanged();
    }

    bool WalletDB::getSystemStateID(Block::SystemState::ID& stateID) const
    {
        return getVar(SystemStateIDName, stateID);
    }

    Height WalletDB::getCurrentHeight() const
    {
        Block::SystemState::ID id = {};
        if (getSystemStateID(id))
        {
            return id.m_Height;
        }
        return 0;
    }

    uint64_t WalletDB::getKnownStateCount() const
    {
        uint64_t count = 0;
        {
            sqlite::Statement stm(_db, "SELECT COUNT(DISTINCT confirmHash) FROM " STORAGE_NAME " ;");
            stm.step();
            stm.get(0, count);
        }
        return count;
    }

    Block::SystemState::ID WalletDB::getKnownStateID(Height height)
    {
        Block::SystemState::ID id = wallet::GetEmptyID();
        const char* req = "SELECT DISTINCT confirmHeight, confirmHash FROM " STORAGE_NAME " WHERE confirmHeight >= ?2 LIMIT 1 OFFSET ?1;";

        sqlite::Statement stm(_db, req);
        stm.bind(1, height);
        stm.bind(2, Rules::HeightGenesis);
        if (stm.step())
        {
            stm.get(0, id.m_Height);
            stm.get(1, id.m_Hash);
        }
        return id;
    }

    void WalletDB::rollbackConfirmedUtxo(Height minHeight)
    {
        sqlite::Transaction trans(_db);

        {
            const char* req = "UPDATE " STORAGE_NAME " SET status=?1, confirmHeight=?2, lockedHeight=?2, confirmHash=NULL WHERE confirmHeight > ?3 ;";
            sqlite::Statement stm(_db, req);
            stm.bind(1, Coin::Unconfirmed);
            stm.bind(2, MaxHeight);
            stm.bind(3, minHeight);
            stm.step();
        }

        {
            const char* req = "UPDATE " STORAGE_NAME " SET status=?1, lockedHeight=?2 WHERE lockedHeight > ?3 AND confirmHeight <= ?3 ;";
            sqlite::Statement stm(_db, req);
            stm.bind(1, Coin::Unspent);
            stm.bind(2, MaxHeight);
            stm.bind(3, minHeight);
            stm.step();
        }

        trans.commit();
        notifyCoinsChanged();
    }

    vector<TxDescription> WalletDB::getTxHistory(uint64_t start, int count)
    {
        // TODO this is temporary solution
        int txCount = 0;
        {
            sqlite::Statement stm(_db, "SELECT COUNT(DISTINCT txID) FROM " TX_PARAMS_NAME " ;");
            stm.step();
            stm.get(0, txCount);
        }
        
        vector<TxDescription> res;
        if (txCount > 0)
        {
            res.reserve(static_cast<size_t>(min(txCount, count)));
            const char* req = "SELECT DISTINCT txID FROM " TX_PARAMS_NAME " LIMIT ?1 OFFSET ?2 ;";

            sqlite::Statement stm(_db, req);
            stm.bind(1, count);
            stm.bind(2, start);

            while (stm.step())
            {
                TxID txID;
                stm.get(0, txID);
                auto t = getTx(txID);
                if (t.is_initialized())
                {
                    res.emplace_back(*t);
                }
            }
        }

        return res;
    }

    boost::optional<TxDescription> WalletDB::getTx(const TxID& txId)
    {
        const char* req = "SELECT * FROM " TX_PARAMS_NAME " WHERE txID=?1 ;";
        sqlite::Statement stm(_db, req);
        stm.bind(1, txId);

        if (stm.step())
        {
            auto thisPtr = shared_from_this();
            TxDescription tx;
            tx.m_txId = txId;
            bool hasMandatory = wallet::getTxParameter(thisPtr, txId, wallet::TxParameterID::Amount, tx.m_amount)
            && wallet::getTxParameter(thisPtr, txId, wallet::TxParameterID::Fee, tx.m_fee)
            && wallet::getTxParameter(thisPtr, txId, wallet::TxParameterID::MinHeight, tx.m_minHeight)
            && wallet::getTxParameter(thisPtr, txId, wallet::TxParameterID::PeerID, tx.m_peerId)
            && wallet::getTxParameter(thisPtr, txId, wallet::TxParameterID::MyID, tx.m_myId)
            && wallet::getTxParameter(thisPtr, txId, wallet::TxParameterID::CreateTime, tx.m_createTime)
            && wallet::getTxParameter(thisPtr, txId, wallet::TxParameterID::IsSender, tx.m_sender);
            wallet::getTxParameter(thisPtr, txId, wallet::TxParameterID::Message, tx.m_message);
            wallet::getTxParameter(thisPtr, txId, wallet::TxParameterID::Change, tx.m_change);
            wallet::getTxParameter(thisPtr, txId, wallet::TxParameterID::ModifyTime, tx.m_modifyTime);
            wallet::getTxParameter(thisPtr, txId, wallet::TxParameterID::Status, tx.m_status);
            if (hasMandatory)
            {
                return tx;
            }
        }

        return boost::optional<TxDescription>{};
    }

    void WalletDB::saveTx(const TxDescription& p)
    {
        ChangeAction action = ChangeAction::Added;
        sqlite::Transaction trans(_db);
        
        auto thisPtr = shared_from_this();

        wallet::setTxParameter(thisPtr, p.m_txId, wallet::TxParameterID::Amount, p.m_amount);
        wallet::setTxParameter(thisPtr, p.m_txId, wallet::TxParameterID::Fee, p.m_fee);
        wallet::setTxParameter(thisPtr, p.m_txId, wallet::TxParameterID::Change, p.m_change);
        wallet::setTxParameter(thisPtr, p.m_txId, wallet::TxParameterID::MinHeight, p.m_minHeight);
        wallet::setTxParameter(thisPtr, p.m_txId, wallet::TxParameterID::PeerID, p.m_peerId);
        wallet::setTxParameter(thisPtr, p.m_txId, wallet::TxParameterID::MyID, p.m_myId);
        wallet::setTxParameter(thisPtr, p.m_txId, wallet::TxParameterID::Message, p.m_message);
        wallet::setTxParameter(thisPtr, p.m_txId, wallet::TxParameterID::CreateTime, p.m_createTime);
        wallet::setTxParameter(thisPtr, p.m_txId, wallet::TxParameterID::ModifyTime, p.m_modifyTime);
        wallet::setTxParameter(thisPtr, p.m_txId, wallet::TxParameterID::IsSender, p.m_sender);
        wallet::setTxParameter(thisPtr, p.m_txId, wallet::TxParameterID::Status, p.m_status);

        trans.commit();

        notifyTransactionChanged(action, {p});
    }

    void WalletDB::deleteTx(const TxID& txId)
    {
        auto tx = getTx(txId);
        if (tx.is_initialized())
        {
            const char* req = "DELETE FROM " TX_PARAMS_NAME " WHERE txID=?1;";
            sqlite::Statement stm(_db, req);

            stm.bind(1, txId);

            stm.step();
            notifyTransactionChanged(ChangeAction::Removed, { *tx });
        }
    }

    void WalletDB::rollbackTx(const TxID& txId)
    {
        sqlite::Transaction trans(_db);

        {
            const char* req = "UPDATE " STORAGE_NAME " SET status=?3, spentTxId=NULL WHERE spentTxId=?1 AND status=?2;";
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
        notifyCoinsChanged();
    }

    std::vector<TxPeer> WalletDB::getPeers()
    {
        std::vector<TxPeer> peers;
        sqlite::Statement stm(_db, "SELECT * FROM " PEERS_NAME ";");
        while (stm.step())
        {
            auto& peer = peers.emplace_back();
            ENUM_PEER_FIELDS(STM_GET_LIST, NOSEP, peer);
        }
        return peers;
    }

    void WalletDB::addPeer(const TxPeer& peer)
    {
        sqlite::Transaction trans(_db);

        sqlite::Statement stm2(_db, "SELECT * FROM " PEERS_NAME " WHERE walletID=?1;");
        stm2.bind(1, peer.m_walletID);

        const char* updateReq = "UPDATE " PEERS_NAME " SET address=?2, label=?3 WHERE walletID=?1;";
        const char* insertReq = "INSERT INTO " PEERS_NAME " (" ENUM_PEER_FIELDS(LIST, COMMA, ) ") VALUES(" ENUM_PEER_FIELDS(BIND_LIST, COMMA, ) ");";

        sqlite::Statement stm(_db, stm2.step() ? updateReq : insertReq);
        ENUM_PEER_FIELDS(STM_BIND_LIST, NOSEP, peer);
        stm.step();

        trans.commit();
    }

    boost::optional<TxPeer> WalletDB::getPeer(const WalletID& peerID)
    {
        sqlite::Statement stm(_db, "SELECT * FROM " PEERS_NAME " WHERE walletID=?1;");
        stm.bind(1, peerID);
        if (stm.step())
        {
            TxPeer peer = {};
            ENUM_PEER_FIELDS(STM_GET_LIST, NOSEP, peer);
            return peer;
        }
        return boost::optional<TxPeer>{};
    }

    void WalletDB::clearPeers()
    {
        sqlite::Statement stm(_db, "DELETE FROM " PEERS_NAME ";");
        stm.step();
    }

    std::vector<WalletAddress> WalletDB::getAddresses(bool own)
    {
        vector<WalletAddress> res;
        const char* req = "SELECT * FROM " ADDRESSES_NAME " WHERE own=?1 ORDER BY createTime DESC;";

        sqlite::Statement stm(_db, req);
        stm.bind(1, own);

        while (stm.step())
        {
            auto& a = res.emplace_back();
            ENUM_ADDRESS_FIELDS(STM_GET_LIST, NOSEP, a);
        }
        return res;
    }

    void WalletDB::saveAddress(const WalletAddress& address)
    {
        sqlite::Transaction trans(_db);

        {
            const char* selectReq = "SELECT * FROM " ADDRESSES_NAME " WHERE walletID=?1;";
            sqlite::Statement stm2(_db, selectReq);
            stm2.bind(1, address.m_walletID);

            if (stm2.step())
            {
                const char* updateReq = "UPDATE " ADDRESSES_NAME " SET label=?2, category=?3 WHERE walletID=?1;";
                sqlite::Statement stm(_db, updateReq);

                stm.bind(1, address.m_walletID);
                stm.bind(2, address.m_label);
                stm.bind(3, address.m_category);
                stm.step();
            }
            else
            {
                const char* insertReq = "INSERT INTO " ADDRESSES_NAME " (" ENUM_ADDRESS_FIELDS(LIST, COMMA, ) ") VALUES(" ENUM_ADDRESS_FIELDS(BIND_LIST, COMMA, ) ");";
                sqlite::Statement stm(_db, insertReq);
                ENUM_ADDRESS_FIELDS(STM_BIND_LIST, NOSEP, address);
                stm.step();
            }
        }

        trans.commit();

        notifyAddressChanged();
    }

    boost::optional<WalletAddress> WalletDB::getAddress(const WalletID& id)
    {
        const char* req = "SELECT * FROM " ADDRESSES_NAME " WHERE walletID=?1;";
        sqlite::Statement stm(_db, req);

        stm.bind(1, id);

        if (stm.step())
        {
            WalletAddress address = {};
            ENUM_ADDRESS_FIELDS(STM_GET_LIST, NOSEP, address);
            return address;
        }
        return boost::optional<WalletAddress>{};
    }

    void WalletDB::deleteAddress(const WalletID& id)
    {
        const char* req = "DELETE FROM " ADDRESSES_NAME " WHERE walletID=?1;";
        sqlite::Statement stm(_db, req);

        stm.bind(1, id);

        stm.step();

        notifyAddressChanged();
    }

    void WalletDB::subscribe(IWalletDbObserver* observer)
    {
        assert(std::find(m_subscribers.begin(), m_subscribers.end(), observer) == m_subscribers.end());

        m_subscribers.push_back(observer);
    }

    void WalletDB::unsubscribe(IWalletDbObserver* observer)
    {
        auto it = std::find(m_subscribers.begin(), m_subscribers.end(), observer);

        assert(it != m_subscribers.end());

        m_subscribers.erase(it);
    }

	void WalletDB::changePassword(const SecString& password)
	{
		int ret = sqlite3_rekey(_db, password.data(), static_cast<int>(password.size()));
		throwIfError(ret, _db);
	}

    bool WalletDB::setTxParameter(const TxID& txID, wallet::TxParameterID paramID, const ByteBuffer& blob)
    {
        bool hasTx = getTx(txID).is_initialized();
        {
            sqlite::Statement stm(_db, "SELECT * FROM " TX_PARAMS_NAME " WHERE txID=?1 AND paramID=?2;");

            stm.bind(1, txID);
            stm.bind(2, static_cast<int>(paramID));
            if (stm.step())
            {
                // already set
                if (paramID < wallet::TxParameterID::PrivateFirstParam)
                {
                    return false;
                }

                sqlite::Statement stm2(_db, "UPDATE " TX_PARAMS_NAME  " SET value = ?3 WHERE txID = ?1 AND paramID = ?2;");
                stm2.bind(1, txID);
                stm2.bind(2, static_cast<int>(paramID));
                stm2.bind(3, blob);
                stm2.step();
                auto tx = getTx(txID);
                if (tx.is_initialized())
                {
                    notifyTransactionChanged(ChangeAction::Updated, {*tx});
                }
                
                return true;
            }
        }
        
        sqlite::Statement stm(_db, "INSERT INTO " TX_PARAMS_NAME " (" ENUM_TX_PARAMS_FIELDS(LIST, COMMA, ) ") VALUES(" ENUM_TX_PARAMS_FIELDS(BIND_LIST, COMMA, ) ");");
        TxParameter parameter;
        parameter.m_txID = txID;
        parameter.m_paramID = static_cast<int>(paramID);
        parameter.m_value = blob;
        ENUM_TX_PARAMS_FIELDS(STM_BIND_LIST, NOSEP, parameter);
        stm.step();
        auto tx = getTx(txID);
        if (tx.is_initialized())
        {
            notifyTransactionChanged(hasTx ? ChangeAction::Updated : ChangeAction::Added, { *tx });
        }
        return true;
    }

    bool WalletDB::getTxParameter(const TxID& txID, wallet::TxParameterID paramID, ByteBuffer& blob)
    {
        sqlite::Statement stm(_db, "SELECT * FROM " TX_PARAMS_NAME " WHERE txID=?1 AND paramID=?2;");

        stm.bind(1, txID);
        stm.bind(2, static_cast<int>(paramID));

        if (stm.step())
        {
            TxParameter parameter = {};
            ENUM_TX_PARAMS_FIELDS(STM_GET_LIST, NOSEP, parameter);
            blob = move(parameter.m_value);
            return true;
        }
        return false;
    }

    void WalletDB::notifyCoinsChanged()
    {
        for (auto sub : m_subscribers) sub->onCoinsChanged();
    }

    void WalletDB::notifyTransactionChanged(ChangeAction action, vector<TxDescription>&& items)
    {
        for (auto sub : m_subscribers)
        {
            sub->onTransactionChanged(action, move(items));
        }
    }

    void WalletDB::notifySystemStateChanged()
    {
        for (auto sub : m_subscribers) sub->onSystemStateChanged();
    }

    void WalletDB::notifyAddressChanged()
    {
        for (auto sub : m_subscribers) sub->onAddressChanged();
    }

	Block::SystemState::IHistory& WalletDB::get_History()
	{
		return m_History;
	}

	void WalletDB::ShrinkHistory()
	{
		Block::SystemState::Full s;
		if (m_History.get_Tip(s))
		{
			const Height hMaxBacklog = Rules::get().MaxRollbackHeight * 2; // can actually be more

			if (s.m_Height > hMaxBacklog)
			{
				const char* req = "DELETE FROM " TblStates " WHERE " TblStates_Height "<=?";
				sqlite::Statement stm(_db, req);
				stm.bind(1, s.m_Height - hMaxBacklog);
				stm.step();

			}
		}
	}

	bool WalletDB::History::Enum(IWalker& w, const Height* pBelow)
	{
		const char* req = pBelow ?
			"SELECT " TblStates_Hdr " FROM " TblStates " WHERE " TblStates_Height "<? ORDER BY " TblStates_Height " DESC" :
			"SELECT " TblStates_Hdr " FROM " TblStates " ORDER BY " TblStates_Height " DESC";

		sqlite::Statement stm(get_ParentObj()._db, req);

		if (pBelow)
			stm.bind(1, *pBelow);

		while (stm.step())
		{
			Block::SystemState::Full s;
			stm.get(0, s);

			if (!w.OnState(s))
				return false;
		}

		return true;
	}

	bool WalletDB::History::get_At(Block::SystemState::Full& s, Height h)
	{
		const char* req = "SELECT " TblStates_Hdr " FROM " TblStates " WHERE " TblStates_Height "=?";

		sqlite::Statement stm(get_ParentObj()._db, req);
		stm.bind(1, h);

		if (!stm.step())
			return false;

		stm.get(0, s);
		return true;
	}

	void WalletDB::History::AddStates(const Block::SystemState::Full* pS, size_t nCount)
	{
		sqlite::Transaction trans(get_ParentObj()._db);

		const char* req = "INSERT OR REPLACE INTO " TblStates " (" TblStates_Height "," TblStates_Hdr ") VALUES(?,?)";
		sqlite::Statement stm(get_ParentObj()._db, req);

		for (size_t i = 0; i < nCount; i++)
		{
			if (i)
				stm.Reset();

			stm.bind(1, pS[i].m_Height);
			stm.bind(2, pS[i]);
			stm.step();
		}

		trans.commit();

	}

	void WalletDB::History::DeleteFrom(Height h)
	{
		const char* req = "DELETE FROM " TblStates " WHERE " TblStates_Height ">=?";
		sqlite::Statement stm(get_ParentObj()._db, req);
		stm.bind(1, h);
		stm.step();
	}

    namespace wallet
    {
        bool getTxParameter(IWalletDB::Ptr db, const TxID& txID, TxParameterID paramID, ECC::Point::Native& value)
        {
            ECC::Point pt;
            if (getTxParameter(db, txID, paramID, pt))
            {
                return value.Import(pt);
            }
            return false;
        }

        bool getTxParameter(IWalletDB::Ptr db, const TxID& txID, TxParameterID paramID, ECC::Scalar::Native& value)
        {
            ECC::Scalar s;
            if (getTxParameter(db, txID, paramID, s))
            {
                value.Import(s);
                return true;
            }
            return false;
        }

        bool getTxParameter(IWalletDB::Ptr db, const TxID& txID, TxParameterID paramID, ByteBuffer& value)
        {
            return db->getTxParameter(txID, paramID, value);
        }

        bool setTxParameter(IWalletDB::Ptr db, const TxID& txID, TxParameterID paramID, const ECC::Point::Native& value)
        {
            ECC::Point pt;
            if (value.Export(pt))
            {
                return setTxParameter(db, txID, paramID, pt);
            }
            return false;
        }

        bool setTxParameter(IWalletDB::Ptr db, const TxID& txID, TxParameterID paramID, const ECC::Scalar::Native& value)
        {
            ECC::Scalar s;
            value.Export(s);
            return setTxParameter(db, txID, paramID, s);
        }

        bool setTxParameter(IWalletDB::Ptr db, const TxID& txID, TxParameterID paramID, const ByteBuffer& value)
        {
            return db->setTxParameter(txID, paramID, value);
        }

        ByteBuffer toByteBuffer(const ECC::Point::Native& value)
        {
            ECC::Point pt;
            if (value.Export(pt))
            {
                return toByteBuffer(pt);
            }
            return ByteBuffer();
        }

        ByteBuffer toByteBuffer(const ECC::Scalar::Native& value)
        {
            ECC::Scalar s;
            value.Export(s);
            return toByteBuffer(s);
        }

        Amount getAvailable(beam::IWalletDB::Ptr walletDB)
        {
            auto currentHeight = walletDB->getCurrentHeight();
            Amount total = 0;
            walletDB->visit([&total, &currentHeight](const Coin& c)->bool
            {
                Height lockHeight = c.m_maturity;

                if (c.m_status == Coin::Unspent
                    && lockHeight <= currentHeight)
                {
                    total += c.m_amount;
                }
                return true;
            });
            return total;
        }

        Amount getAvailableByType(beam::IWalletDB::Ptr walletDB, Coin::Status status, Key::Type keyType)
        {
            auto currentHeight = walletDB->getCurrentHeight();
            Amount total = 0;
            walletDB->visit([&total, &currentHeight, &status, &keyType](const Coin& c)->bool
            {
                Height lockHeight = c.m_maturity;

                if (c.m_status == status
                    && c.m_key_type == keyType
                    && lockHeight <= currentHeight)
                {
                    total += c.m_amount;
                }
                return true;
            });
            return total;
        }

        Amount getTotal(beam::IWalletDB::Ptr walletDB, Coin::Status status)
        {
            Amount total = 0;
            walletDB->visit([&total, &status](const Coin& c)->bool
            {
                if (c.m_status == status)
                {
                    total += c.m_amount;
                }
                return true;
            });
            return total;
        }

        Amount getTotalByType(beam::IWalletDB::Ptr walletDB, Coin::Status status, Key::Type keyType)
        {
            Amount total = 0;
            walletDB->visit([&total, &status, &keyType](const Coin& c)->bool
            {
                if (c.m_status == status && c.m_key_type == keyType)
                {
                    total += c.m_amount;
                }
                return true;
            });
            return total;
        }
    }
}
