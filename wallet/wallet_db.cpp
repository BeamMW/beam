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
    each(7, confirmHeight, sep, INTEGER, obj) \
    each(8, confirmHash,   sep, BLOB, obj) \
    each(9, createTxId,    sep, BLOB, obj) \
    each(10, spentTxId,    sep, BLOB, obj) \
    each(11, lockedHeight,    , BLOB, obj)            // last item without separator// last item without separator

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
                int ret = sqlite3_bind_blob(_stm, col, hash.m_pData, hash.nBytes, NULL);
                throwIfError(ret, _db);
            }

            void bind(int col, const io::Address& address)
            {
                bind(col, address.u64());
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

            void bind(int col, const string& val) // utf-8
            {
                int ret = sqlite3_bind_text(_stm, col, val.data(), -1, NULL);
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

            void get(int col, TxID& id)
            {
                int size = 0;
                get(col, static_cast<void*>(id.data()), size);
                assert(size == id.size());
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
        const char* Version = "Version";
        const char* SystemStateIDName = "SystemStateID";
        const char* LastUpdateTimeName = "LastUpdateTime";
        const int BusyTimeoutMs = 1000;
        const int DbVersion = 5;
    }

    Coin::Coin(const Amount& amount, Status status, const Height& createHeight, const Height& maturity, KeyType keyType, Height confirmHeight, Height lockedHeight)
        : m_id{ 0 }
        , m_amount{ amount }
        , m_status{ status }
        , m_createHeight{ createHeight }
        , m_maturity{ maturity }
        , m_key_type{ keyType }
        , m_confirmHeight{ confirmHeight }
        , m_confirmHash(Zero)
        , m_lockedHeight{ lockedHeight }
    {
        assert(isValid());
    }

    Coin::Coin()
        : Coin(0, Coin::Unspent, 0, MaxHeight, KeyType::Regular, MaxHeight)
    {
        assert(isValid());
    }

    bool Coin::isReward() const
    {
        return m_key_type == KeyType::Coinbase || m_key_type == KeyType::Comission;
    }

    bool Coin::isValid() const
    {
        return m_createHeight <= m_maturity
            && m_maturity <= m_lockedHeight
            && m_createHeight <= m_confirmHeight;
    }

    bool Keychain::isInitialized(const string& path)
    {
#ifdef WIN32
        return boost::filesystem::exists(Utf8toUtf16(path.c_str()));
#else
        return boost::filesystem::exists(path);
#endif
    }

    IKeyChain::Ptr Keychain::init(const string& path, const SecString& password, const ECC::NoLeak<ECC::uintBig>& secretKey)
    {
        if (!isInitialized(path))
        {
            auto keychain = make_shared<Keychain>(secretKey);

            {
                int ret = sqlite3_open_v2(path.c_str(), &keychain->_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_CREATE, NULL);
                throwIfError(ret, keychain->_db);
            }

            {
                int ret = sqlite3_key(keychain->_db, password.data(), password.size());
                throwIfError(ret, keychain->_db);
            }

            {
                const char* req = "CREATE TABLE " STORAGE_NAME " (" ENUM_ALL_STORAGE_FIELDS(LIST_WITH_TYPES, COMMA,) ");"
                                  "CREATE INDEX ConfirmIndex ON " STORAGE_NAME"(confirmHeight);"
                                  "CREATE INDEX SpentIndex ON " STORAGE_NAME"(lockedHeight);";
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
                const char* req = "CREATE TABLE " PEERS_NAME " (" ENUM_PEER_FIELDS(LIST_WITH_TYPES, COMMA, ) ") WITHOUT ROWID;";
                int ret = sqlite3_exec(keychain->_db, req, NULL, NULL, NULL);
                throwIfError(ret, keychain->_db);
            }

            {
                const char* req = "CREATE TABLE " ADDRESSES_NAME " (" ENUM_ADDRESS_FIELDS(LIST_WITH_TYPES, COMMA, ) ") WITHOUT ROWID;";
                int ret = sqlite3_exec(keychain->_db, req, NULL, NULL, NULL);
                throwIfError(ret, keychain->_db);
            }

            {
                const char* req = "CREATE TABLE " TX_PARAMS_NAME " (" ENUM_TX_PARAMS_FIELDS(LIST_WITH_TYPES, COMMA, ) ", PRIMARY KEY (txID, paramID)) WITHOUT ROWID;";
                int ret = sqlite3_exec(keychain->_db, req, NULL, NULL, NULL);
                throwIfError(ret, keychain->_db);
            }

            {
                keychain->setVar(WalletSeed, secretKey.V);
                keychain->setVar(Version, DbVersion);
            }

            return static_pointer_cast<IKeyChain>(keychain);
        }

        LOG_ERROR() << path << " already exists.";

        return Ptr();
    }

    IKeyChain::Ptr Keychain::open(const string& path, const SecString& password)
    {
        try
        {
            if (isInitialized(path))
            {
                ECC::NoLeak<ECC::uintBig> seed;
                seed.V = Zero;
                auto keychain = make_shared<Keychain>(seed);

                {
                    int ret = sqlite3_open_v2(path.c_str(), &keychain->_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX, NULL);
                    throwIfError(ret, keychain->_db);
                }

                {
                    int ret = sqlite3_key(keychain->_db, password.data(), password.size());
                    throwIfError(ret, keychain->_db);
                }
                {
                    int ret = sqlite3_busy_timeout(keychain->_db, BusyTimeoutMs);
                    throwIfError(ret, keychain->_db);
                }
                {
                    int version = 0;
                    if (!keychain->getVar(Version, version) || version > DbVersion)
                    {
                        LOG_DEBUG() << "Invalid DB version: " << version << ". Expected: " << DbVersion;
                        return Ptr();
                    }
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

                {
                    const char* req = "CREATE TABLE IF NOT EXISTS " TX_PARAMS_NAME " (" ENUM_TX_PARAMS_FIELDS(LIST_WITH_TYPES, COMMA, ) ", PRIMARY KEY (txID, paramID)) WITHOUT ROWID;";
                    int ret = sqlite3_exec(keychain->_db, req, NULL, NULL, NULL);
                    throwIfError(ret, keychain->_db);
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
        }
        catch (const runtime_error&)
        {

        }

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
        DeriveKey(key, m_kdf, coin.m_createHeight, coin.m_key_type, (coin.m_key_type == KeyType::Regular) ? coin.m_id : 0);
        return key;
    }

    void Keychain::get_IdentityKey(ECC::Scalar::Native& sk) const
    {
        DeriveKey(sk, m_kdf, 0, KeyType::Identity);
    }

    vector<beam::Coin> Keychain::selectCoins(const Amount& amount, bool lock)
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

            notifyKeychainChanged();
        }
        std::sort(coins.begin(), coins.end(), [](const Coin& lhs, const Coin& rhs) {return lhs.m_amount < rhs.m_amount; });
        return coins;
    }

    std::vector<beam::Coin> Keychain::getCoinsCreatedByTx(const TxID& txId)
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
        assert(coin.m_amount > 0 && coin.isValid());
        if (coin.m_key_type == KeyType::Coinbase
            || coin.m_key_type == KeyType::Comission)
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

        const char* req = "INSERT INTO " STORAGE_NAME " (" ENUM_STORAGE_FIELDS(LIST, COMMA, ) ") VALUES(" ENUM_STORAGE_FIELDS(BIND_LIST, COMMA, ) ");";
        sqlite::Statement stm(_db, req);

        ENUM_STORAGE_FIELDS(STM_BIND_LIST, NOSEP, coin);

        stm.step();

        coin.m_id = getLastID(_db);

        notifyKeychainChanged();
    }

    void Keychain::update(const beam::Coin& coin)
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

        notifyKeychainChanged();
    }

    void Keychain::update(const vector<beam::Coin>& coins)
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
            notifyKeychainChanged();
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

            notifyKeychainChanged();
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

        notifyKeychainChanged();
    }

    void Keychain::clear()
    {
        {
            sqlite::Statement stm(_db, "DELETE FROM " STORAGE_NAME ";");
            stm.step();
            notifyKeychainChanged();
        }

        {
            sqlite::Statement stm(_db, "DELETE FROM " HISTORY_NAME ";");
            stm.step();
            notifyTransactionChanged(ChangeAction::Reset, {});
        }
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

    bool Keychain::getBlob(const char* name, ByteBuffer& var) const
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

    Timestamp Keychain::getLastUpdateTime() const
    {
        Timestamp timestamp = {};
        if (getVar(LastUpdateTimeName, timestamp))
        {
            return timestamp;
        }
        return 0;
    }

    void Keychain::setSystemStateID(const Block::SystemState::ID& stateID)
    {
        setVar(SystemStateIDName, stateID);
        setVar(LastUpdateTimeName, getTimestamp());
        notifySystemStateChanged();
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

    uint64_t Keychain::getKnownStateCount() const
    {
        uint64_t count = 0;
        {
            sqlite::Statement stm(_db, "SELECT COUNT(DISTINCT confirmHash) FROM " STORAGE_NAME " ;");
            stm.step();
            stm.get(0, count);
        }
        return count;
    }

    Block::SystemState::ID Keychain::getKnownStateID(Height height)
    {
        Block::SystemState::ID id = {};
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

    void Keychain::rollbackConfirmedUtxo(Height minHeight)
    {
        sqlite::Transaction trans(_db);

        {
            const char* req = "DELETE FROM " STORAGE_NAME " WHERE createHeight >?1 AND (key_type=?2 OR key_type=?3);";
            sqlite::Statement stm(_db, req);
            stm.bind(1, minHeight);
            stm.bind(2, KeyType::Coinbase);
            stm.bind(3, KeyType::Comission);
            stm.step();
        }

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
        notifyKeychainChanged();
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

    boost::optional<TxDescription> Keychain::getTx(const TxID& txId)
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
        ChangeAction action = ChangeAction::Added;
        sqlite::Transaction trans(_db);

        {
            const char* selectReq = "SELECT * FROM " HISTORY_NAME " WHERE txId=?1;";
            sqlite::Statement stm2(_db, selectReq);
            stm2.bind(1, p.m_txId);

            if (stm2.step())
            {
                const char* updateReq = "UPDATE " HISTORY_NAME " SET modifyTime=?2, status=?3, fsmState=?4, minHeight=?5, change=?6 WHERE txId=?1;";
                sqlite::Statement stm(_db, updateReq);

                stm.bind(1, p.m_txId);
                stm.bind(2, p.m_modifyTime);
                stm.bind(3, p.m_status);
                stm.bind(4, p.m_fsmState);
                stm.bind(5, p.m_minHeight);
                stm.bind(6, p.m_change);
                stm.step();
                action = ChangeAction::Updated;
            }
            else
            {
                const char* insertReq = "INSERT INTO " HISTORY_NAME " (" ENUM_HISTORY_FIELDS(LIST, COMMA,) ") VALUES(" ENUM_HISTORY_FIELDS(BIND_LIST, COMMA,) ");";
                sqlite::Statement stm(_db, insertReq);
                ENUM_HISTORY_FIELDS(STM_BIND_LIST, NOSEP, p);
                stm.step();
            }
        }

        trans.commit();

        notifyTransactionChanged(action, {p});
    }

    void Keychain::deleteTx(const TxID& txId)
    {
        auto tx = getTx(txId);
        if (tx.is_initialized())
        {
            const char* req = "DELETE FROM " HISTORY_NAME " WHERE txId=?1;";
            sqlite::Statement stm(_db, req);

            stm.bind(1, txId);

            stm.step();
            notifyTransactionChanged(ChangeAction::Removed, { *tx });
        }
    }

    void Keychain::rollbackTx(const TxID& txId)
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
        notifyKeychainChanged();
    }

    std::vector<TxPeer> Keychain::getPeers()
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

    void Keychain::addPeer(const TxPeer& peer)
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

    boost::optional<TxPeer> Keychain::getPeer(const WalletID& peerID)
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

    void Keychain::clearPeers()
    {
        sqlite::Statement stm(_db, "DELETE FROM " PEERS_NAME ";");
        stm.step();
    }

    std::vector<WalletAddress> Keychain::getAddresses(bool own)
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

    void Keychain::saveAddress(const WalletAddress& address)
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

    boost::optional<WalletAddress> Keychain::getAddress(const WalletID& id)
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

    void Keychain::deleteAddress(const WalletID& id)
    {
        const char* req = "DELETE FROM " ADDRESSES_NAME " WHERE walletID=?1;";
        sqlite::Statement stm(_db, req);

        stm.bind(1, id);

        stm.step();

        notifyAddressChanged();
    }

    void Keychain::subscribe(IKeyChainObserver* observer)
    {
        assert(std::find(m_subscribers.begin(), m_subscribers.end(), observer) == m_subscribers.end());

        m_subscribers.push_back(observer);
    }

    void Keychain::unsubscribe(IKeyChainObserver* observer)
    {
        auto it = std::find(m_subscribers.begin(), m_subscribers.end(), observer);

        assert(it != m_subscribers.end());

        m_subscribers.erase(it);
    }

    void Keychain::changePassword(const SecString& password)
    {
        int ret = sqlite3_rekey(_db, password.data(), password.size());
        throwIfError(ret, _db);
    }

    void Keychain::setTxParameter(const TxID& txID, int paramID, const ByteBuffer& blob)
    {
        {
            sqlite::Statement stm(_db, "SELECT * FROM " TX_PARAMS_NAME " WHERE txID=?1 AND paramID=?2;");

            stm.bind(1, txID);
            stm.bind(2, paramID);
            if (stm.step())
            {
                return;
            }
        }
        
        sqlite::Statement stm(_db, "INSERT INTO " TX_PARAMS_NAME " (" ENUM_TX_PARAMS_FIELDS(LIST, COMMA, ) ") VALUES(" ENUM_TX_PARAMS_FIELDS(BIND_LIST, COMMA, ) ");");
        TxParameter parameter;
        parameter.m_txID = txID;
        parameter.m_paramID = paramID;
        parameter.m_value = blob;
        ENUM_TX_PARAMS_FIELDS(STM_BIND_LIST, NOSEP, parameter);
        stm.step();
    }

    bool Keychain::getTxParameter(const TxID& txID, int paramID, ByteBuffer& blob)
    {
        sqlite::Statement stm(_db, "SELECT * FROM " TX_PARAMS_NAME " WHERE txID=?1 AND paramID=?2;");

        stm.bind(1, txID);
        stm.bind(2, paramID);

        if (stm.step())
        {
            TxParameter parameter = {};
            ENUM_TX_PARAMS_FIELDS(STM_GET_LIST, NOSEP, parameter);
            blob = move(parameter.m_value);
            return true;
        }
        return false;
    }

    void Keychain::notifyKeychainChanged()
    {
        for (auto sub : m_subscribers) sub->onKeychainChanged();
    }

    void Keychain::notifyTransactionChanged(ChangeAction action, vector<TxDescription>&& items)
    {
        for (auto sub : m_subscribers)
        {
            sub->onTransactionChanged(action, move(items));
        }
    }

    void Keychain::notifySystemStateChanged()
    {
        for (auto sub : m_subscribers) sub->onSystemStateChanged();
    }

    void Keychain::notifyAddressChanged()
    {
        for (auto sub : m_subscribers) sub->onAddressChanged();
    }

    namespace wallet
    {
        Amount getAvailable(beam::IKeyChain::Ptr keychain)
        {
            auto currentHeight = keychain->getCurrentHeight();
            Amount total = 0;
            keychain->visit([&total, &currentHeight](const Coin& c)->bool
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

        Amount getAvailableByType(beam::IKeyChain::Ptr keychain, Coin::Status status, KeyType keyType)
        {
            auto currentHeight = keychain->getCurrentHeight();
            Amount total = 0;
            keychain->visit([&total, &currentHeight, &status, &keyType](const Coin& c)->bool
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

        Amount getTotal(beam::IKeyChain::Ptr keychain, Coin::Status status)
        {
            Amount total = 0;
            keychain->visit([&total, &status](const Coin& c)->bool
            {
                if (c.m_status == status)
                {
                    total += c.m_amount;
                }
                return true;
            });
            return total;
        }

        Amount getTotalByType(beam::IKeyChain::Ptr keychain, Coin::Status status, KeyType keyType)
        {
            Amount total = 0;
            keychain->visit([&total, &status, &keyType](const Coin& c)->bool
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
