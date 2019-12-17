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
#include "utility/helpers.h"
#include "sqlite/sqlite3.h"
#include "core/block_rw.h"
#include <sstream>
#include <boost/functional/hash.hpp>
#include <boost/filesystem.hpp>
#include "nlohmann/json.hpp"
#include "utility/std_extension.h"

#define NOSEP
#define COMMA ", "
#define AND " AND "


#define ENUM_STORAGE_ID(each, sep, obj) \
    each(Type,           ID.m_Type,     INTEGER NOT NULL, obj) sep \
    each(SubKey,         ID.m_SubIdx,   INTEGER NOT NULL, obj) sep \
    each(Number,         ID.m_Idx,      INTEGER NOT NULL, obj) sep \
    each(amount,         ID.m_Value,    INTEGER NOT NULL, obj) \

#define ENUM_STORAGE_FIELDS(each, sep, obj) \
    each(maturity,       maturity,      INTEGER NOT NULL, obj) sep \
    each(confirmHeight,  confirmHeight, INTEGER, obj) sep \
    each(spentHeight,    spentHeight,   INTEGER, obj) sep \
    each(createTxId,     createTxId,    BLOB, obj) sep \
    each(spentTxId,      spentTxId,     BLOB, obj) sep \
    each(sessionId,      sessionId,     INTEGER NOT NULL, obj)

#define ENUM_ALL_STORAGE_FIELDS(each, sep, obj) \
    ENUM_STORAGE_ID(each, sep, obj) sep \
    ENUM_STORAGE_FIELDS(each, sep, obj)

#define LIST(name, member, type, obj) #name
#define LIST_WITH_TYPES(name, member, type, obj) #name " " #type

#define STM_BIND_LIST(name, member, type, obj) stm.bind(++colIdx, obj .m_ ## member);
#define STM_GET_LIST(name, member, type, obj) stm.get(colIdx++, obj .m_ ## member);

#define BIND_LIST(name, member, type, obj) "?"
#define SET_LIST(name, member, type, obj) #name "=?"

#define STORAGE_FIELDS ENUM_ALL_STORAGE_FIELDS(LIST, COMMA, )

#define STORAGE_WHERE_ID " WHERE " ENUM_STORAGE_ID(SET_LIST, AND, )
#define STORAGE_BIND_ID(obj) ENUM_STORAGE_ID(STM_BIND_LIST, NOSEP, obj)

#define STORAGE_NAME "storage"
#define VARIABLES_NAME "variables"
#define ADDRESSES_NAME "addresses"
#define TX_PARAMS_NAME "txparams"
#define PRIVATE_VARIABLES_NAME "PrivateVariables"
#define WALLET_MESSAGE_NAME "WalletMessages"
#define INCOMING_WALLET_MESSAGE_NAME "IncomingWalletMessages"

#define ENUM_VARIABLES_FIELDS(each, sep, obj) \
    each(name,  name,  TEXT UNIQUE, obj) sep \
    each(value, value, BLOB, obj)

#define VARIABLES_FIELDS ENUM_VARIABLES_FIELDS(LIST, COMMA, )

#define ENUM_ADDRESS_FIELDS(each, sep, obj) \
    each(walletID,       walletID,       BLOB NOT NULL PRIMARY KEY, obj) sep \
    each(label,          label,          TEXT NOT NULL, obj) sep \
    each(category,       category,       TEXT, obj) sep \
    each(createTime,     createTime,     INTEGER, obj) sep \
    each(duration,       duration,       INTEGER, obj) sep \
    each(OwnID,          OwnID,          INTEGER NOT NULL, obj)

#define ADDRESS_FIELDS ENUM_ADDRESS_FIELDS(LIST, COMMA, )

#define ENUM_TX_PARAMS_FIELDS(each, sep, obj) \
    each(txID,           txID,           BLOB NOT NULL , obj) sep \
    each(subTxID,        subTxID,        INTEGER NOT NULL , obj) sep \
    each(paramID,        paramID,        INTEGER NOT NULL , obj) sep \
    each(value,          value,          BLOB, obj)

#define TX_PARAMS_FIELDS ENUM_TX_PARAMS_FIELDS(LIST, COMMA, )

#define ENUM_WALLET_MESSAGE_FIELDS(each, sep, obj) \
    each(ID,  ID,  INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, obj) sep \
    each(PeerID, PeerID,   BLOB, obj) sep \
    each(Message, Message, BLOB, obj)

#define WALLET_MESSAGE_FIELDS ENUM_WALLET_MESSAGE_FIELDS(LIST, COMMA, )

#define ENUM_INCOMING_WALLET_MESSAGE_FIELDS(each, sep, obj) \
    each(ID,  ID,  INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, obj) sep \
    each(Channel, Channel, INTEGER, obj) sep \
    each(Message, Message, BLOB, obj)

#define INCOMING_WALLET_MESSAGE_FIELDS ENUM_INCOMING_WALLET_MESSAGE_FIELDS(LIST, COMMA, )

#define TblStates            "States"
#define TblStates_Height     "Height"
#define TblStates_Hdr        "State"

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

namespace beam::wallet
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
            if (res == SQLITE_NOTADB)
            {
                throw FileIsNotDatabaseException();
            }
            throw DatabaseException(ss.str());
        }


        void enterKey(sqlite3 * db, const SecString& password)
        {
            if (password.size() > static_cast<size_t>(numeric_limits<int>::max()))
            {
                throwIfError(SQLITE_TOOBIG, db);
            }
            int ret = sqlite3_key(db, password.data(), static_cast<int>(password.size()));
            throwIfError(ret, db);
        }

        struct CoinSelector3
        {
            typedef std::vector<Coin> Coins;
            typedef std::vector<size_t> Indexes;

            using Result = pair<Amount, Indexes>;

            const Coins& m_Coins; // input coins must be in ascending order, without zeroes
            
            CoinSelector3(const Coins& coins)
                :m_Coins(coins)
            {
            }

            static const uint32_t s_Factor = 16;

            struct Partial
            {
                static const Amount s_Inf = Amount(-1);

                struct Link {
                    size_t m_iNext; // 1-based, to distinguish "NULL" pointers
                    size_t m_iElement;
                };

                std::vector<Link> m_vLinks;

                struct Slot {
                    size_t m_iTop;
                    Amount m_Sum;
                };

                Slot m_pSlots[s_Factor + 1];
                Amount m_Goal;

                void Reset()
                {
                    m_vLinks.clear();
                    ZeroObject(m_pSlots);
                }

                uint32_t get_Slot(Amount v) const
                {
                    uint64_t i = v * s_Factor / m_Goal; // TODO - overflow check!
                    uint32_t res = (i >= s_Factor) ? s_Factor : static_cast<uint32_t>(i);
                    assert(res <= s_Factor + 1);
                    return res;
                }

                void Append(Slot& rDst, Amount v, size_t i0)
                {
                    m_vLinks.emplace_back();
                    m_vLinks.back().m_iElement = i0;
                    m_vLinks.back().m_iNext = rDst.m_iTop;

                    rDst.m_iTop = m_vLinks.size();
                    rDst.m_Sum += v;
                }

                bool IsBetter(Amount v, uint32_t iDst) const
                {
                    const Slot& rDst = m_pSlots[iDst];
                    return (s_Factor == iDst) ?
                        (!rDst.m_Sum || (v < rDst.m_Sum)) :
                        (v > rDst.m_Sum);
                }

                void AddItem(Amount v, size_t i0)
                {
                    // try combining first. Go from higher to lower, to make sure we don't process a slot which already contains this item
                    for (uint32_t iSrc = s_Factor; iSrc--; )
                    {
                        Slot& rSrc = m_pSlots[iSrc];
                        if (!rSrc.m_Sum)
                            continue;

                        Amount v2 = rSrc.m_Sum + v;
                        uint32_t iDst = get_Slot(v2);

                        if (!IsBetter(v2, iDst))
                            continue;

                        Slot& rDst = m_pSlots[iDst];

                        // improve
                        if (iSrc != iDst)
                            rDst = rSrc; // copy

                        Append(rDst, v, i0);
                    }

                    // try as-is
                    uint32_t iDst = get_Slot(v);
                    if (IsBetter(v, iDst))
                    {
                        Slot& rDst = m_pSlots[iDst];
                        ZeroObject(rDst);
                        Append(rDst, v, i0);
                    }
                }
            };

            void SolveOnce(Partial& part, Amount goal, size_t iEnd)
            {
                assert((goal > 0) && (iEnd <= m_Coins.size()));
                part.Reset();
                part.m_Goal = goal;

                for (size_t i = iEnd; i--; )
                    part.AddItem(m_Coins[i].m_ID.m_Value, i);
            }

            Result Select(Amount amount)
            {
                Partial part;
                size_t iEnd = m_Coins.size();

                Amount nOvershootPrev = Amount(-1);

                Result res;
                for (res.first = 0; (res.first < amount) && iEnd; )
                {
                    Amount goal = amount - res.first;
                    SolveOnce(part, goal, iEnd);

                    Partial::Slot& r1 = part.m_pSlots[s_Factor];
                    // reverse list direction
                    size_t iPrev = 0;
                    for (size_t i = r1.m_iTop; i; )
                    {
                        Partial::Link& link = part.m_vLinks[i - 1];
                        size_t iNext = link.m_iNext;
                        link.m_iNext = iPrev;
                        iPrev = i;
                        i = iNext;
                    }
                    r1.m_iTop = iPrev;

                    if (r1.m_Sum < goal)
                    {
                        // no solution
                        assert(!r1.m_Sum && !res.first);

                        // return the maximum we have
                        uint32_t iSlot = s_Factor - 1;
                        for ( ; iSlot > 0; iSlot--)
                            if (part.m_pSlots[iSlot].m_Sum)
                                break;

                        res.first = part.m_pSlots[iSlot].m_Sum;

                        for (size_t iLink = part.m_pSlots[iSlot].m_iTop; iLink; )
                        {
                            const Partial::Link& link = part.m_vLinks[iLink - 1];
                            iLink = link.m_iNext;

                            assert(link.m_iElement < iEnd);
                            res.second.push_back(link.m_iElement);
                        }

                        return res;
                    }

                    Amount nOvershoot = r1.m_Sum - goal;
                    bool bShouldRetry = (nOvershoot < nOvershootPrev);
                    nOvershootPrev = nOvershoot;

                    for (size_t iLink = r1.m_iTop; iLink; )
                    {
                        const Partial::Link& link = part.m_vLinks[iLink - 1];
                        iLink = link.m_iNext;

                        assert(link.m_iElement < iEnd);
                        res.second.push_back(link.m_iElement);
                        iEnd = link.m_iElement;

                        Amount v = m_Coins[link.m_iElement].m_ID.m_Value;
                        res.first += v;

                        if (bShouldRetry && (amount <= res.first + nOvershoot*2))
                            break; // leave enough window for reorgs
                    }
                }

                return res;
            }


        };

        template<typename T>
        void deserialize(T& value, const ByteBuffer& blob)
        {
            if (!blob.empty())
            {
                Deserializer d;
                d.reset(blob.data(), blob.size());
                d & value;
            }
            else
            {
                ZeroObject(value);
            }
        }

        void deserialize(ByteBuffer& value, ByteBuffer& blob)
        {
            value = blob;
        }
    }

    namespace sqlite
    {
        struct Statement
        {
            Statement(const WalletDB* db, const char* sql, bool privateDB=false)
                : _walletDB(nullptr)
                , _db(privateDB ? db->m_PrivateDB : db->_db)
                , _stm(nullptr)
            {
                int ret = sqlite3_prepare_v2(_db, sql, -1, &_stm, nullptr);
                throwIfError(ret, _db);
            }

            Statement(WalletDB* db, const char* sql, bool privateDB = false)
                : _walletDB(db)
                , _db(privateDB ? db->m_PrivateDB : db->_db)
                , _stm(nullptr)
            {
                if (_walletDB)
                {
                    _walletDB->onPrepareToModify();
                }
                int ret = sqlite3_prepare_v2(_db, sql, -1, &_stm, nullptr);
                throwIfError(ret, _db);
            }

            void Reset()
            {
                sqlite3_clear_bindings(_stm);
                sqlite3_reset(_stm);
            }

            void bind(int col, int val)
            {
                int ret = sqlite3_bind_int(_stm, col, val);
                throwIfError(ret, _db);
            }

            void bind(int col, Key::Type val)
            {
                int ret = sqlite3_bind_int(_stm, col, val);
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

            void bind(int col, uint32_t val)
            {
                int ret = sqlite3_bind_int(_stm, col, val);
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

            void bind(int col, const WalletID& x)
            {
                bind(col, &x, sizeof(x));
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
                if (size > static_cast<size_t>(numeric_limits<int32_t>::max()))// 0x7fffffff
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

            void bind(int col, TxParameterID val)
            {
                int ret = sqlite3_bind_int(_stm, col, static_cast<int>(val));
                throwIfError(ret, _db);
            }

            bool step()
            {
                int n = _walletDB ? sqlite3_total_changes(_db) : 0;
                int ret = sqlite3_step(_stm);
                if (_walletDB && sqlite3_total_changes(_db) != n)
                {
                    _walletDB->onModified();
                }
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

            void get(int col, uint32_t& val)
            {
                val = sqlite3_column_int(_stm, col);
            }

            void get(int col, int& val)
            {
                val = sqlite3_column_int(_stm, col);
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
                TxID val;
                if (getBlobSafe(col, &val, sizeof(val)))
                    id = val;
            }

            void get(int col, ECC::Hash::Value& hash)
            {
                getBlobStrict(col, hash.m_pData, hash.nBytes);
            }

            void get(int col, WalletID& x)
            {
                getBlobStrict(col, &x, sizeof(x));
            }

            void get(int col, io::Address& address)
            {
                uint64_t t = 0;;
                get(col, t);
                address = io::Address::from_u64(t);
            }
            void get(int col, ByteBuffer& b)
            {
                b.clear();

                int size = sqlite3_column_bytes(_stm, col);
                if (size > 0)
                {
                    b.resize(size);
                    memcpy(&b.front(), sqlite3_column_blob(_stm, col), size);
                }
            }

            bool getBlobSafe(int col, void* blob, int size)
            {
                if (sqlite3_column_bytes(_stm, col) != size)
                    return false;

                memcpy(blob, sqlite3_column_blob(_stm, col), size);
                return true;
            }

            void getBlobStrict(int col, void* blob, int size)
            {
                if (!getBlobSafe(col, blob, size))
                    throw std::runtime_error("wdb corruption");
            }

            void get(int col, Key::Type& type)
            {
                type = sqlite3_column_int(_stm, col);
            }

            void get(int col, string& str) // utf-8
            {
                int size = sqlite3_column_bytes(_stm, col);
                if (size > 0)
                {
                    const unsigned char* data = sqlite3_column_text(_stm, col);
                    str.assign(reinterpret_cast<const string::value_type*>(data));
                }
            }

            const char* retrieveSQL()
            {
                return sqlite3_expanded_sql(_stm);
            }

            ~Statement()
            {
                sqlite3_finalize(_stm);
            }
        private:
            WalletDB* _walletDB;
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

            void rollback() noexcept
            {
                int ret = sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
                _rollbacked = (ret == SQLITE_OK);
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
        const char* OwnerKey = "OwnerKey";
        const char* Version = "Version";
        const char* SystemStateIDName = "SystemStateID";
        const char* LastUpdateTimeName = "LastUpdateTime";
        const int BusyTimeoutMs = 5000;
        const int DbVersion = 15;
        const int DbVersion14 = 14;
        const int DbVersion13 = 13;
        const int DbVersion12 = 12;
        const int DbVersion11 = 11;
        const int DbVersion10 = 10;
    }

    Coin::Coin(Amount amount /* = 0 */, Key::Type keyType /* = Key::Type::Regular */)
        : m_status{ Status::Unavailable }
        , m_maturity{ MaxHeight }
        , m_confirmHeight{ MaxHeight }
        , m_spentHeight{ MaxHeight }
        , m_sessionId(EmptyCoinSession)
    {
        m_ID = Zero;
        m_ID.m_Value = amount;
        m_ID.m_Type = keyType;
    }

    bool Coin::isReward() const
    {
        switch (m_ID.m_Type)
        {
        case Key::Type::Coinbase:
        case Key::Type::Comission:
            return true;
        default:
            return false;
        }
    }

    bool Coin::IsMaturityValid() const
    {
        switch (m_status)
        {
        case Unavailable:
        case Incoming:
            return false;

        default:
            return true;
        }
    }

    Height Coin::get_Maturity() const
    {
        return IsMaturityValid() ? m_maturity : MaxHeight;
    }

    bool Coin::operator==(const Coin& other) const
    {
        return other.m_ID == m_ID;
    }

    bool Coin::operator!=(const Coin& other) const
    {
        return !(other == *this);
    }

    string Coin::toStringID() const
    {
        ID::Packed packed;
        packed = m_ID;

        return to_hex(&packed, sizeof(packed));
    }

    Amount Coin::getAmount() const
    {
        return m_ID.m_Value;
    }

    std::string Coin::getStatusString() const
    {
        static std::map<Status, std::string> Strings 
        {
            {Unavailable,   "unavailable"},
            {Available,     "available"},
            {Maturing,      "maturing"},
            {Outgoing,      "outgoing"},
            {Incoming,      "incoming"},
            {Spent,         "spent"},
        };

        return Strings[m_status];
    }

    boost::optional<Coin::ID> Coin::FromString(const std::string& str)
    {
        bool isValid = false;
        auto byteBuffer = from_hex(str, &isValid);
        if (isValid && byteBuffer.size() <= sizeof(Coin::ID::Packed))
        {
            Coin::ID::Packed packed;
            ZeroObject(packed);
            uint8_t* p = reinterpret_cast<uint8_t*>(&packed) + sizeof(Coin::ID::Packed) - byteBuffer.size();
            copy_n(byteBuffer.begin(), byteBuffer.size(), p);
            Coin::ID id;
            id = packed;
            return id;
        }
        return boost::optional<Coin::ID>();
    }

    bool WalletDB::isInitialized(const string& path)
    {
#ifdef WIN32
        return boost::filesystem::exists(Utf8toUtf16(path.c_str()));
#else
        return boost::filesystem::exists(path);
#endif
    }

    namespace
    {
        bool IsTableCreated(WalletDB* db, const char* tableName)
        {
            std::string req = "SELECT name FROM sqlite_master WHERE type='table' AND name='";
            req += tableName;
            req += "';";

            sqlite::Statement stm(db, req.c_str(), false);
            return stm.step();
        }

        void CreateStorageTable(sqlite3* db)
        {
            const char* req = "CREATE TABLE " STORAGE_NAME " (" ENUM_ALL_STORAGE_FIELDS(LIST_WITH_TYPES, COMMA, ) ");"
                "CREATE UNIQUE INDEX CoinIndex ON " STORAGE_NAME "(" ENUM_STORAGE_ID(LIST, COMMA, )  ");"
                "CREATE INDEX ConfirmIndex ON " STORAGE_NAME"(confirmHeight);";
            int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, db);
        }

        void CreateWalletMessageTable(sqlite3* db)
        {
            {
                const char* req = "CREATE TABLE IF NOT EXISTS " WALLET_MESSAGE_NAME " (" ENUM_WALLET_MESSAGE_FIELDS(LIST_WITH_TYPES, COMMA, ) ");";
                int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
                throwIfError(ret, db);
            }
            {
                const char* req = "CREATE TABLE IF NOT EXISTS " INCOMING_WALLET_MESSAGE_NAME " (" ENUM_INCOMING_WALLET_MESSAGE_FIELDS(LIST_WITH_TYPES, COMMA, ) ");";
                int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
                throwIfError(ret, db);
            }
        }

        void CreatePrivateVariablesTable(sqlite3* db)
        {
            const char* req = "CREATE TABLE " PRIVATE_VARIABLES_NAME " (" ENUM_VARIABLES_FIELDS(LIST_WITH_TYPES, COMMA, ) ");";
            int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, db);
        }
    
        void CreateAddressesTable(sqlite3* db)
        {
            const char* req = "CREATE TABLE " ADDRESSES_NAME " (" ENUM_ADDRESS_FIELDS(LIST_WITH_TYPES, COMMA, ) ") WITHOUT ROWID;";
            int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, db);
        }

        void CreateVariablesTable(sqlite3* db)
        {
            const char* req = "CREATE TABLE " VARIABLES_NAME " (" ENUM_VARIABLES_FIELDS(LIST_WITH_TYPES, COMMA, ) ");";
            int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, db);
        }

        void CreateTxParamsTable(sqlite3* db)
        {
            const char* req = "CREATE TABLE " TX_PARAMS_NAME " (" ENUM_TX_PARAMS_FIELDS(LIST_WITH_TYPES, COMMA, ) ", PRIMARY KEY (txID, subTxID, paramID)) WITHOUT ROWID;";
            int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, db);
        }

        void CreateStatesTable(sqlite3* db)
        {
            const char* req = "CREATE TABLE [" TblStates "] ("
                "[" TblStates_Height    "] INTEGER NOT NULL PRIMARY KEY,"
                "[" TblStates_Hdr        "] BLOB NOT NULL)";
            int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, db);
        }

        void OpenAndMigrateIfNeeded(const string& path, sqlite3** db, const SecString& password)
        {
            int ret = sqlite3_open_v2(path.c_str(), db, SQLITE_OPEN_READWRITE, nullptr);
            throwIfError(ret, *db);
            enterKey(*db, password);
            // try to decrypt
            ret = sqlite3_exec(*db, "PRAGMA user_version;", nullptr, nullptr, nullptr);
            if (ret != SQLITE_OK)
            {
                LOG_INFO() << "Applying PRAGMA cipher_migrate...";
                ret = sqlite3_close(*db);
                throwIfError(ret, *db);
                ret = sqlite3_open_v2(path.c_str(), db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
                throwIfError(ret, *db);
                enterKey(*db, password);
                ret = sqlite3_exec(*db, "PRAGMA cipher_migrate; ", nullptr, nullptr, nullptr);
                throwIfError(ret, *db);
            }
        }

        bool MoveSeedToPrivateVariables(WalletDB& db)
        {
            ECC::NoLeak<ECC::Hash::Value> seed;
            if (!storage::getVar(db, WalletSeed, seed.V))
            {
                assert(false && "there is no seed for walletDB");
                LOG_ERROR() << "there is no seed for walletDB";
                return false;
            }
            db.setPrivateVarRaw(WalletSeed, &seed.V, sizeof(seed.V));

            {
                sqlite::Statement stm(&db, "DELETE FROM " VARIABLES_NAME " WHERE name=?1;");
                stm.bind(1, WalletSeed);
                stm.step();
            }

            return true;
        }

        bool GetPrivateVarRaw(const WalletDB& db, const char* name, void* data, int size, bool privateDb)
        {
            {
                sqlite::Statement stm(&db, "SELECT name FROM sqlite_master WHERE type = 'table' AND name = '" PRIVATE_VARIABLES_NAME "';", privateDb);
                if (!stm.step())
                {
                    return false; // public database
                }
            }

            {
                const char* req = "SELECT value FROM " PRIVATE_VARIABLES_NAME " WHERE name=?1;";

                sqlite::Statement stm(&db, req, privateDb);
                stm.bind(1, name);

                return stm.step() && stm.getBlobSafe(0, data, size);
            }
        }

        bool DropPrivateVariablesFromPublicDatabase(WalletDB& db)
        {
            {
                sqlite::Statement stm(&db, "SELECT name FROM sqlite_master WHERE type='table' AND name='" PRIVATE_VARIABLES_NAME "';");

                if (!stm.step())
                {
                    return true; // there is nothing to drop
                }
            }

            // ensure that we have  master key in private database
            {
                ECC::NoLeak<ECC::Hash::Value> seed; // seed from public db
                if (GetPrivateVarRaw(db, WalletSeed, &seed.V, sizeof(ECC::Hash::Value), false))
                {
                    ECC::NoLeak<ECC::Hash::Value> seed2; // seed from private db
                    if (GetPrivateVarRaw(db, WalletSeed, &seed2.V, sizeof(ECC::Hash::Value), true))
                    {
                        if (seed.V != seed2.V)
                        {
                            LOG_ERROR() << "Public database has different master key. Please check your \'wallet.db\' and \'wallet.db.private\'";
                            return false;
                        }
                    }
                    else
                    {
                        db.setPrivateVarRaw(WalletSeed, &seed.V, sizeof(seed.V));
                    }
                }
            }

            sqlite::Statement dropStm(&db, "DROP TABLE " PRIVATE_VARIABLES_NAME ";");
            dropStm.step();
            return true;
        }
    }

    void WalletDB::createTables(sqlite3* db, sqlite3* privateDb)
    {
        CreateStorageTable(db);
        CreateWalletMessageTable(db);
        CreatePrivateVariablesTable(privateDb);
        CreateVariablesTable(db);
        CreateAddressesTable(db);
        CreateTxParamsTable(db);
        CreateStatesTable(db);
    }

    IWalletDB::Ptr WalletDB::init(const string& path, const SecString& password, const ECC::NoLeak<ECC::uintBig>& secretKey, io::Reactor::Ptr reactor, bool separateDBForPrivateData)
    {
        if (!isInitialized(path))
        {
            sqlite3* db = nullptr;
            {
                int ret = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
                throwIfError(ret, db);
            }

            sqlite3* sdb = db;

            if (separateDBForPrivateData)
            {
                int ret = sqlite3_open_v2((path+".private").c_str(), &sdb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
                throwIfError(ret, sdb);
                enterKey(sdb, password);
            }

            enterKey(db, password);
            auto walletDB = make_shared<WalletDB>(db, secretKey, reactor, sdb);

            createTables(walletDB->_db, walletDB->m_PrivateDB);

            {
                // store master key
                walletDB->setPrivateVarRaw(WalletSeed, &secretKey.V, sizeof(secretKey.V));

                // store owner key (public)
                {
                    Key::IKdf::Ptr pKey = walletDB->get_MasterKdf();
                    const ECC::HKdf& kdf = static_cast<ECC::HKdf&>(*pKey);

                    auto publicKdf = make_shared<ECC::HKdfPub>();
                    publicKdf->GenerateFrom(kdf);
                    ECC::NoLeak<ECC::HKdfPub::Packed> packedOwnerKey;
                    publicKdf->Export(packedOwnerKey.V);

                    storage::setVar(*walletDB, OwnerKey, packedOwnerKey.V);
                    walletDB->m_OwnerKdf = publicKdf;
                }

                storage::setVar(*walletDB, Version, DbVersion);
            }

            walletDB->flushDB();

            return static_pointer_cast<IWalletDB>(walletDB);
        }

        LOG_ERROR() << path << " already exists.";

        return Ptr();
    }

#if defined(BEAM_HW_WALLET)
    IWalletDB::Ptr WalletDB::initWithTrezor(const string& path, std::shared_ptr<ECC::HKdfPub> ownerKey, const SecString& password, io::Reactor::Ptr reactor)
    {
        if (!isInitialized(path))
        {
            sqlite3* db = nullptr;
            {
                int ret = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
                throwIfError(ret, db);
            }

            enterKey(db, password);
            auto walletDB = make_shared<WalletDB>(db, reactor);

            createTables(walletDB->_db, walletDB->m_PrivateDB);

            {
                // store owner key (public)
                {
                    ECC::NoLeak<ECC::HKdfPub::Packed> packedOwnerKey;
                    ownerKey->Export(packedOwnerKey.V);

                    storage::setVar(*walletDB, OwnerKey, packedOwnerKey.V);
                    walletDB->m_OwnerKdf = ownerKey;
                }

                storage::setVar(*walletDB, Version, DbVersion);
            }

            walletDB->flushDB();

            return static_pointer_cast<IWalletDB>(walletDB);
        }

        LOG_ERROR() << path << " already exists.";

        return Ptr();
    }
#endif

    IWalletDB::Ptr WalletDB::open(const string& path, const SecString& password, io::Reactor::Ptr reactor, bool useTrezor)
    {
        if (!isInitialized(path))
        {
            LOG_ERROR() << path << " not found, please init the wallet before.";
            throw DatabaseNotFoundException();
        }

        sqlite3 *db = nullptr;
        OpenAndMigrateIfNeeded(path, &db, password);
        sqlite3 *sdb = db;
        string privatePath = path + ".private";
        bool separateDBForPrivateData = isInitialized(privatePath);
        if (separateDBForPrivateData)
        {
            OpenAndMigrateIfNeeded(privatePath, &sdb, password);
        }

        auto walletDB = make_shared<WalletDB>(db, reactor, sdb);
        {
            int ret = sqlite3_busy_timeout(walletDB->_db, BusyTimeoutMs);
            throwIfError(ret, walletDB->_db);
        }
        {
            int version = 0;
            storage::getVar(*walletDB, Version, version);

            // migration
            try
            {
                switch (version)
                {
                case DbVersion10:
                case DbVersion11:
                case DbVersion12:
                    {
                        LOG_INFO() << "Converting DB from format 10-11";

                        // storage table changes: removed [status], [createHeight], [lockedHeight], added [spentHeight]
                        // sqlite doesn't support column removal. So instead we'll rename this table, select the data, and insert it to the new table
                        //
                        // The missing data, [spentHeight] - can only be deduced strictly if the UTXO has a reference to the spending tx. Otherwise we'll have to put a dummy spentHeight.
                        // In case of a rollback there's a chance (albeit small) we won't notice the UTXO un-spent status. But in case of such a problem this should be fixed by the "UTXO rescan".

                        if (!IsTableCreated(walletDB.get(), STORAGE_NAME "_del"))
                        {
                            const char* req =
                                "ALTER TABLE " STORAGE_NAME " RENAME TO " STORAGE_NAME "_del;"
                                "DROP INDEX CoinIndex;"
                                "DROP INDEX ConfirmIndex;";

                            int ret = sqlite3_exec(walletDB->_db, req, NULL, NULL, NULL);
                            throwIfError(ret, walletDB->_db);
                        }

                        if (!IsTableCreated(walletDB.get(), STORAGE_NAME))
                        {
                            CreateStorageTable(walletDB->_db);
                        }

                        {
                            const char* req = "SELECT * FROM " STORAGE_NAME "_del;";
                                
                            for (sqlite::Statement stm(walletDB.get(), req);  stm.step(); )
                            {
                                Coin coin;
                                stm.get(0, coin.m_ID.m_Type);
                                stm.get(1, coin.m_ID.m_SubIdx);
                                stm.get(2, coin.m_ID.m_Idx);
                                stm.get(3, coin.m_ID.m_Value);

                                uint32_t status = 0;
                                stm.get(4, status);

                                stm.get(5, coin.m_maturity);
                                // createHeight - skip
                                stm.get(7, coin.m_confirmHeight);
                                // lockedHeight - skip
                                stm.get(9, coin.m_createTxId);
                                stm.get(10, coin.m_spentTxId);
                                stm.get(11, coin.m_sessionId);

                                if (Coin::Status::Spent == static_cast<Coin::Status>(status))
                                {
                                    // try to guess the spentHeight
                                    coin.m_spentHeight = coin.m_maturity; // init guess

                                    if (coin.m_spentTxId)
                                    {
                                        // we cannot use getTxParameter since it uses newer db scheme
                                        //storage::getTxParameter(*walletDB, coin.m_spentTxId.get(), TxParameterID::KernelProofHeight, coin.m_spentHeight);
                                        sqlite::Statement stm2(walletDB.get(), "SELECT value FROM " TX_PARAMS_NAME " WHERE txID=?1 AND paramID=?2;");
                                        stm2.bind(1, coin.m_spentTxId.get());
                                        stm2.bind(2, TxParameterID::KernelProofHeight);

                                        if (stm2.step())
                                        {
                                            ByteBuffer buf;
                                            stm2.get(0, buf);
                                            Height h = 0;
                                            if (fromByteBuffer(buf, h))
                                            {
                                                coin.m_spentHeight = h;
                                            }
                                        }
                                    }
                                }

                                walletDB->saveCoin(coin);
                            }
                        }

                        {
                            const char* req = "DROP TABLE " STORAGE_NAME "_del;";
                            int ret = sqlite3_exec(walletDB->_db, req, NULL, NULL, NULL);
                            throwIfError(ret, walletDB->_db);
                        }
                    }

                    // no break;

                case DbVersion13:
                    LOG_INFO() << "Converting DB to format 13...";

                    CreateWalletMessageTable(walletDB->_db);
                    CreatePrivateVariablesTable(walletDB->m_PrivateDB);

                    if (!MoveSeedToPrivateVariables(*walletDB))
                    {
                        throw DatabaseException("failed to move seed to private valiables");
                    }

                case DbVersion14:
                    {
                        LOG_INFO() << "Converting DB to format 14...";

                        // tx_params table changed: added new column [subTxID]
                        // move old data to temp table
                        {
                            const char* req = "ALTER TABLE " TX_PARAMS_NAME " RENAME TO " TX_PARAMS_NAME "_del;";
                            int ret = sqlite3_exec(walletDB->_db, req, NULL, NULL, NULL);
                            throwIfError(ret, walletDB->_db);
                        }

                        // create new table
                        CreateTxParamsTable(walletDB->_db);

                        // migration
                        {
                            const char* req = "INSERT INTO " TX_PARAMS_NAME " (" ENUM_TX_PARAMS_FIELDS(LIST, COMMA, ) ") SELECT \"txID\", ?1 as \"subTxID\", \"paramID\", \"value\" FROM " TX_PARAMS_NAME "_del;";
                            sqlite::Statement stm(walletDB.get(), req);
                            stm.bind(1, kDefaultSubTxID);
                            stm.step();
                        }

                        // remove tmp table
                        {
                            const char* req = "DROP TABLE " TX_PARAMS_NAME "_del;";
                            int ret = sqlite3_exec(walletDB->_db, req, NULL, NULL, NULL);
                            throwIfError(ret, walletDB->_db);
                        }

                    }

                    storage::setVar(*walletDB, Version, DbVersion);
                    // no break;

                case DbVersion:

                    // drop private variables from public database for cold wallet 
                    if (separateDBForPrivateData && !DropPrivateVariablesFromPublicDatabase(*walletDB))
                    {
                        throw DatabaseException("failed to drop private variables from public database");
                    }

                    break; // ok

                default:
                    {
                        LOG_DEBUG() << "Invalid DB version: " << version << ". Expected: " << DbVersion;
                        throw InvalidDatabaseVersionException();
                    }
                }

                walletDB->flushDB();
            }
            catch (...)
            {
                LOG_ERROR() << "Database migration failed";
                walletDB->rollbackDB();
                throw DatabaseMigrationException();
            }
        }
        {
            const char* req = "SELECT name FROM sqlite_master WHERE type='table' AND name='" STORAGE_NAME "';";
            int ret = sqlite3_exec(walletDB->_db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, walletDB->_db);
        }

        {
            const char* req = "SELECT " STORAGE_FIELDS " FROM " STORAGE_NAME ";";
            int ret = sqlite3_exec(walletDB->_db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, walletDB->_db);
        }

        {
            const char* req = "SELECT " VARIABLES_FIELDS " FROM " VARIABLES_NAME ";";
            int ret = sqlite3_exec(walletDB->_db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, walletDB->_db);
        }
        {
            ECC::NoLeak<ECC::Hash::Value> seed;
            if (walletDB->getPrivateVarRaw(WalletSeed, &seed.V, sizeof(seed.V)))
            {
                ECC::HKdf::Create(walletDB->m_pKdf, seed.V);
                walletDB->m_OwnerKdf = walletDB->m_pKdf;
            }
            else
            {
                ECC::NoLeak<ECC::HKdfPub::Packed> packedOwnerKey;
                if (storage::getVar(*walletDB, OwnerKey, packedOwnerKey.V))
                {
                    auto publicKdf = make_shared<ECC::HKdfPub>();
                    if (!publicKdf->Import(packedOwnerKey.V))
                    {
                        LOG_ERROR() << "Failed to load owner key";
                        return Ptr();
                    }
                    walletDB->m_OwnerKdf = publicKdf;
                }

            }
        }

        walletDB->m_useTrezor = useTrezor;
        return static_pointer_cast<IWalletDB>(walletDB);
    }

    WalletDB::WalletDB(sqlite3* db, io::Reactor::Ptr reactor)
        : WalletDB(db, reactor, db)
    {

    }

    WalletDB::WalletDB(sqlite3* db, io::Reactor::Ptr reactor, sqlite3* sdb)
        : _db(db)
        , m_PrivateDB(sdb)
        , m_Reactor(reactor)
        , m_IsFlushPending(false)
        , m_mandatoryTxParams{
            TxParameterID::TransactionType,
            TxParameterID::Amount,
            TxParameterID::MyID,
            TxParameterID::CreateTime,
            TxParameterID::IsSender }
    {

    }

    WalletDB::WalletDB(sqlite3* db, const ECC::NoLeak<ECC::uintBig>& secretKey, io::Reactor::Ptr reactor, sqlite3* sdb)
        : WalletDB(db, reactor, sdb)
    {
        ECC::HKdf::Create(m_pKdf, secretKey.V);
    }

    WalletDB::~WalletDB()
    {
        if (_db)
        {
            if (m_DbTransaction)
            {
                try
                {
                    m_DbTransaction->commit();
                }
                catch (const runtime_error& ex)
                {
                    LOG_ERROR() << "Wallet DB Commit failed: " << ex.what();
                }
                m_DbTransaction.reset();
            }
            BEAM_VERIFY(SQLITE_OK == sqlite3_close(_db));
            if (m_PrivateDB && _db != m_PrivateDB)
            {
                BEAM_VERIFY(SQLITE_OK == sqlite3_close(m_PrivateDB));
                m_PrivateDB = nullptr;
            }
            _db = nullptr;
        }
        
    }

    Key::IKdf::Ptr WalletDB::get_MasterKdf() const
    {
        return m_useTrezor ? nullptr : m_pKdf;
    }

	Key::IKdf::Ptr IWalletDB::get_ChildKdf(const Key::IDV& kidv) const
	{
		return MasterKey::get_Child(get_MasterKdf(), kidv);
	}

    beam::Key::IPKdf::Ptr WalletDB::get_OwnerKdf() const
    {
        return m_OwnerKdf;
    }

    void IWalletDB::calcCommitment(ECC::Scalar::Native& sk, ECC::Point& comm, const Coin::ID& cid)
    {
        SwitchCommitment().Create(sk, comm, *get_ChildKdf(cid), cid);
    }

	void IWalletDB::ImportRecovery(const std::string& path)
	{
		IRecoveryProgress prog;
		BEAM_VERIFY(ImportRecovery(path, prog));
	}

	bool IWalletDB::ImportRecovery(const std::string& path, IRecoveryProgress& prog)
	{
		beam::RecoveryInfo::Reader rp;
		rp.Open(path.c_str());
		uint64_t nTotal = rp.m_Stream.get_Remaining();

		beam::Key::IPKdf::Ptr pOwner = get_MasterKdf();

		while (true)
		{
			RecoveryInfo::Entry x;
			if (!rp.Read(x))
				break;

			uint64_t nRemaining = rp.m_Stream.get_Remaining();
			if (!prog.OnProgress(nTotal - nRemaining, nTotal))
				return false;

			Key::IDV kidv;
			if (!x.m_Output.Recover(x.m_CreateHeight, *pOwner, kidv))
				continue;

			if (!kidv.m_Value && (Key::Type::Decoy == kidv.m_Type))
				continue; // filter-out decoys

			ECC::Scalar::Native sk;
			ECC::Point comm;
			calcCommitment(sk, comm, kidv);
			if (!(comm == x.m_Output.m_Commitment))
				continue;

			Coin c;
			c.m_ID = kidv;
			findCoin(c); // in case it exists already - fill its parameters

			c.m_maturity = x.m_Output.get_MinMaturity(x.m_CreateHeight);
			c.m_confirmHeight = x.m_CreateHeight;

			LOG_INFO() << "CoinID: " << c.m_ID << " Maturity=" << c.m_maturity << " Recovered";

			saveCoin(c);
		}

		rp.Finalyze(); // final verification

		// add states to history
		std::vector<Block::SystemState::Full> vec;
		rp.m_Cwp.UnpackStates(vec);

		if (!vec.empty())
			get_History().AddStates(&vec.front(), vec.size());

		return true;
	}

    vector<Coin> WalletDB::selectCoins(Amount amount)
    {
        vector<Coin> coins, coinsSel;
        Block::SystemState::ID stateID = {};
        getSystemStateID(stateID);

        {
            sqlite::Statement stm(this, "SELECT " STORAGE_FIELDS " FROM " STORAGE_NAME " WHERE maturity>=0 AND maturity<=?1 AND spentHeight<0 ORDER BY amount ASC");
            stm.bind(1, stateID.m_Height);

            while (stm.step())
            {
                auto& coin = coins.emplace_back();
                int colIdx = 0;
                ENUM_ALL_STORAGE_FIELDS(STM_GET_LIST, NOSEP, coin);

                storage::DeduceStatus(*this, coin, stateID.m_Height);
                if (Coin::Status::Available != coin.m_status)
                    coins.pop_back();
                else
                {
                    if (coin.m_ID.m_Value >= amount)
                        break;
                }
            }
        }

        CoinSelector3 csel(coins);
        CoinSelector3::Result res = csel.Select(amount);

        if (res.first >= amount)
        {
            coinsSel.reserve(res.second.size());

            for (size_t j = 0; j < res.second.size(); j++)
                coinsSel.push_back(std::move(coins[res.second[j]]));
        }


        return coinsSel;
    }

    std::vector<Coin> WalletDB::getCoinsCreatedByTx(const TxID& txId)
    {
        // select all coins for TxID
        sqlite::Statement stm(this, "SELECT " STORAGE_FIELDS " FROM " STORAGE_NAME " WHERE createTxID=?1 ORDER BY amount DESC;");
        stm.bind(1, txId);

        vector<Coin> coins;

        while (stm.step())
        {
            auto& coin = coins.emplace_back();
            int colIdx = 0;
            ENUM_ALL_STORAGE_FIELDS(STM_GET_LIST, NOSEP, coin);
        }

        return coins;
    }

    std::vector<Coin> WalletDB::getCoinsByTx(const TxID& txId)
    {
        sqlite::Statement stm(this, "SELECT " STORAGE_FIELDS " FROM " STORAGE_NAME " WHERE createTxID=?1 OR spentTxID=?1;");
        stm.bind(1, txId);

        vector<Coin> coins;

        while (stm.step())
        {
            auto& coin = coins.emplace_back();
            int colIdx = 0;
            ENUM_ALL_STORAGE_FIELDS(STM_GET_LIST, NOSEP, coin);
        }

        return coins;
    }

    vector<Coin> WalletDB::getCoinsByID(const CoinIDList& ids)
    {
        vector<Coin> coins;
        coins.reserve(ids.size());
        struct DummyWrapper {
                Coin::ID m_ID;
        };

        Block::SystemState::ID stateID = {};
        getSystemStateID(stateID);

        for (const auto& cid : ids)
        {
            const char* req = "SELECT * FROM " STORAGE_NAME STORAGE_WHERE_ID;
            sqlite::Statement stm(this, req);

            static_assert(sizeof(DummyWrapper) == sizeof(cid), "");
            const DummyWrapper& wrp = reinterpret_cast<const DummyWrapper&>(cid);

            int colIdx = 0;
            STORAGE_BIND_ID(wrp);

            if (stm.step())
            {
                Coin coin;
                colIdx = 0;
                ENUM_ALL_STORAGE_FIELDS(STM_GET_LIST, NOSEP, coin);
                storage::DeduceStatus(*this, coin, stateID.m_Height);
                if (Coin::Status::Available == coin.m_status)
                {
                    coins.push_back(coin);
                }
            }
        }
        return coins;
    }

    void WalletDB::insertCoinRaw(const Coin& coin)
    {
        const char* req = "INSERT INTO " STORAGE_NAME " (" ENUM_ALL_STORAGE_FIELDS(LIST, COMMA, ) ") VALUES(" ENUM_ALL_STORAGE_FIELDS(BIND_LIST, COMMA, ) ");";
        sqlite::Statement stm(this, req);

        int colIdx = 0;
        ENUM_ALL_STORAGE_FIELDS(STM_BIND_LIST, NOSEP, coin);
        stm.step();
    }

    void WalletDB::insertNewCoin(Coin& coin)
    {
        Coin cDup;
        cDup.m_ID = coin.m_ID;
        while (findCoin(cDup))
            cDup.m_ID.m_Idx++;

        coin.m_ID.m_Idx = cDup.m_ID.m_Idx;
        insertCoinRaw(coin);
    }

    bool WalletDB::updateCoinRaw(const Coin& coin)
    {
        const char* req = "UPDATE " STORAGE_NAME " SET " ENUM_STORAGE_FIELDS(SET_LIST, COMMA, ) STORAGE_WHERE_ID  ";";
        sqlite::Statement stm(this, req);

        int colIdx = 0;
        ENUM_STORAGE_FIELDS(STM_BIND_LIST, NOSEP, coin);
        ENUM_STORAGE_ID(STM_BIND_LIST, NOSEP, coin);
        stm.step();

        return sqlite3_changes(_db) > 0;
    }

    void WalletDB::saveCoinRaw(const Coin& coin)
    {
        if (!updateCoinRaw(coin))
            insertCoinRaw(coin);
    }

    Coin WalletDB::generateSharedCoin(Amount amount)
    {
        Coin coin(amount);

        coin.m_ID.m_Idx = get_RandomID();
        return coin;
    }

    void WalletDB::storeCoin(Coin& coin)
    {
        coin.m_ID.m_Idx = get_RandomID();
        insertNewCoin(coin);
        notifyCoinsChanged();
    }

    void WalletDB::storeCoins(std::vector<Coin>& coins)
    {
        if (coins.empty())
            return;

        uint64_t nKeyIndex = get_RandomID();
        for (auto& coin : coins)
        {
            coin.m_ID.m_Idx = nKeyIndex;
            insertNewCoin(coin);
            nKeyIndex = coin.m_ID.m_Idx + 1;
        }
        notifyCoinsChanged();
    }

    void WalletDB::saveCoin(const Coin& coin)
    {
        saveCoinRaw(coin);
        notifyCoinsChanged();
    }

    void WalletDB::saveCoins(const vector<Coin>& coins)
    {
        if (coins.empty())
            return;

        for (auto& coin : coins)
        {
            saveCoinRaw(coin);
        }

        notifyCoinsChanged();
    }

    uint64_t WalletDB::get_RandomID()
    {
        uintBigFor<uint64_t>::Type val;
        ECC::GenRandom(val);

        uint64_t ret;
        val.Export(ret);
        return ret;
    }

    uint64_t WalletDB::AllocateKidRange(uint64_t nCount)
    {
        // a bit akward, but ok
        static const char szName[] = "LastKid";

        uint64_t nLast;
        uintBigFor<uint64_t>::Type var;

        if (storage::getVar(*this, szName, var))
        {
            var.Export(nLast);
        }
        else
        {
            nLast = getTimestamp(); // by default initialize by current time X1M (1sec resolution) to prevent collisions after reinitialization. Should be ok if creating less than 1M keys / sec average
            nLast *= 1000000;
        }

        var = nLast + nCount;
        storage::setVar(*this, szName, var);

        return nLast;
    }

    void WalletDB::removeCoins(const vector<Coin::ID>& coins)
    {
        if (coins.size())
        {
            for (const auto& cid : coins)
                removeCoinImpl(cid);

            notifyCoinsChanged();
        }
    }

    void WalletDB::removeCoinImpl(const Coin::ID& cid)
    {
        const char* req = "DELETE FROM " STORAGE_NAME STORAGE_WHERE_ID;
        sqlite::Statement stm(this, req);

        struct DummyWrapper {
            Coin::ID m_ID;
        };

        static_assert(sizeof(DummyWrapper) == sizeof(cid), "");
        const DummyWrapper& wrp = reinterpret_cast<const DummyWrapper&>(cid);

        int colIdx = 0;
        STORAGE_BIND_ID(wrp)

        stm.step();
    }

    void WalletDB::removeCoin(const Coin::ID& cid)
    {
        removeCoinImpl(cid);
        notifyCoinsChanged();
    }

    void WalletDB::clearCoins()
    {
        sqlite::Statement stm(this, "DELETE FROM " STORAGE_NAME ";");
        stm.step();
        notifyCoinsChanged();
    }

    bool WalletDB::findCoin(Coin& coin)
    {
        const char* req = "SELECT " ENUM_STORAGE_FIELDS(LIST, COMMA, ) " FROM " STORAGE_NAME STORAGE_WHERE_ID;
        sqlite::Statement stm(this, req);

        int colIdx = 0;
        STORAGE_BIND_ID(coin)

        if (!stm.step())
            return false;

        colIdx = 0;
        ENUM_STORAGE_FIELDS(STM_GET_LIST, NOSEP, coin);

        storage::DeduceStatus(*this, coin, getCurrentHeight());

        return true;
    }

    void WalletDB::visitCoins(function<bool(const Coin& coin)> func)
    {
        const char* req = "SELECT " STORAGE_FIELDS " FROM " STORAGE_NAME " ORDER BY ROWID;";
        sqlite::Statement stm(this, req);

        Height h = getCurrentHeight();

        while (stm.step())
        {
            Coin coin;

            int colIdx = 0;
            ENUM_ALL_STORAGE_FIELDS(STM_GET_LIST, NOSEP, coin);

            storage::DeduceStatus(*this, coin, h);

            if (!func(coin))
                break;
        }
    }

    void WalletDB::setVarRaw(const char* name, const void* data, size_t size)
    {
        const char* req = "INSERT or REPLACE INTO " VARIABLES_NAME " (" VARIABLES_FIELDS ") VALUES(?1, ?2);";

        sqlite::Statement stm(this, req);

        stm.bind(1, name);
        stm.bind(2, data, size);

        stm.step();
    }

    bool WalletDB::getVarRaw(const char* name, void* data, int size) const
    {
        const char* req = "SELECT value FROM " VARIABLES_NAME " WHERE name=?1;";

        sqlite::Statement stm(this, req);
        stm.bind(1, name);

        return
            stm.step() &&
            stm.getBlobSafe(0, data, size);
    }

    void WalletDB::removeVarRaw(const char* name)
    {
        const char* req = "DELETE FROM " VARIABLES_NAME " WHERE name=?1;";
        sqlite::Statement stm(this, req);

        stm.bind(1, name);
        stm.step();
    }

    void WalletDB::setPrivateVarRaw(const char* name, const void* data, size_t size)
    {
        const char* req = "INSERT or REPLACE INTO " PRIVATE_VARIABLES_NAME " (" VARIABLES_FIELDS ") VALUES(?1, ?2);";

        sqlite::Statement stm(this, req, true);

        stm.bind(1, name);
        stm.bind(2, data, size);

        stm.step();
    }

    bool WalletDB::getPrivateVarRaw(const char* name, void* data, int size) const
    {
        return GetPrivateVarRaw(*this, name, data, size, true);
    }

    bool WalletDB::getBlob(const char* name, ByteBuffer& var) const
    {
        const char* req = "SELECT value FROM " VARIABLES_NAME " WHERE name=?1;";

        sqlite::Statement stm(this, req);
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
        
        if (storage::getVar(*this, LastUpdateTimeName, timestamp))
        {
            return timestamp;
        }
        return 0;
    }

    void WalletDB::setSystemStateID(const Block::SystemState::ID& stateID)
    {
        storage::setVar(*this, SystemStateIDName, stateID);
        storage::setVar(*this, LastUpdateTimeName, getTimestamp());
        notifySystemStateChanged(stateID);
    }

    bool WalletDB::getSystemStateID(Block::SystemState::ID& stateID) const
    {
        return storage::getVar(*this, SystemStateIDName, stateID);
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

    void WalletDB::rollbackConfirmedUtxo(Height minHeight)
    {
        // UTXOs
        {
            const char* req = "UPDATE " STORAGE_NAME " SET confirmHeight=?1 WHERE confirmHeight > ?2;";
            sqlite::Statement stm(this, req);
            stm.bind(1, MaxHeight);
            stm.bind(2, minHeight);
            stm.step();
        }

        {
            const char* req = "UPDATE " STORAGE_NAME " SET spentHeight=?1 WHERE spentHeight > ?2;";
            sqlite::Statement stm(this, req);
            stm.bind(1, MaxHeight);
            stm.bind(2, minHeight);
            stm.step();
        }

        notifyCoinsChanged();
    }

    vector<TxDescription> WalletDB::getTxHistory(wallet::TxType txType, uint64_t start, int count) const
    {
        // TODO this is temporary solution
        int txCount = 0;
        {
            std::string req = "SELECT COUNT(DISTINCT txID) FROM " TX_PARAMS_NAME " WHERE paramID = ?1";
            req += (txType != wallet::TxType::ALL) ? " AND value = ?2 ;" : " ;";

            sqlite::Statement stm(this, req.c_str());
            stm.bind(1, wallet::TxParameterID::TransactionType);

            ByteBuffer typeBlob;
            if (txType != wallet::TxType::ALL)
            {
                typeBlob = toByteBuffer(txType);
                stm.bind(2, typeBlob);
            }

            stm.step();
            stm.get(0, txCount);
        }

        vector<TxDescription> res;
        if (txCount > 0)
        {
            res.reserve(static_cast<size_t>(min(txCount, count)));
            std::string req = "SELECT DISTINCT txID FROM " TX_PARAMS_NAME;

            if (txType != wallet::TxType::ALL)
            {
                req += " WHERE paramID = ?3 AND value = ?4";
            }

            req += " LIMIT ?1 OFFSET ?2 ;";

            sqlite::Statement stm(this, req.c_str());
            stm.bind(1, count);
            stm.bind(2, start);

            ByteBuffer typeBlob;
            if (txType != wallet::TxType::ALL)
            {
                stm.bind(3, wallet::TxParameterID::TransactionType);
                typeBlob = toByteBuffer(txType);
                stm.bind(4, typeBlob);
            }

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
            sort(res.begin(), res.end(), [](const auto& left, const auto& right) {return left.m_createTime > right.m_createTime; });
        }

        return res;
    }
    
    boost::optional<TxDescription> WalletDB::getTx(const TxID& txId) const
    {
        // load only simple TX that supported by TxDescription
        const char* req = "SELECT * FROM " TX_PARAMS_NAME " WHERE txID=?1;";
        sqlite::Statement stm(this, req);
        stm.bind(1, txId);

        TxDescription txDescription(txId);
        std::set<TxParameterID> gottenParams;

        while (stm.step())
        {
            TxParameter parameter = {};
            int colIdx = 0;
            ENUM_TX_PARAMS_FIELDS(STM_GET_LIST, NOSEP, parameter);
            auto parameterID = static_cast<TxParameterID>(parameter.m_paramID);

            txDescription.SetParameter(parameterID, parameter.m_value, static_cast<SubTxID>(parameter.m_subTxID));

            if (parameter.m_subTxID == kDefaultSubTxID)
            {
                gottenParams.emplace(parameterID);

                switch (parameterID)
                {
                case TxParameterID::TransactionType:
                    deserialize(txDescription.m_txType, parameter.m_value);
                    break;
                case TxParameterID::Amount:
                    deserialize(txDescription.m_amount, parameter.m_value);
                    break;
                case TxParameterID::Fee:
                    deserialize(txDescription.m_fee, parameter.m_value);
                    break;
                case TxParameterID::MinHeight:
                    deserialize(txDescription.m_minHeight, parameter.m_value);
                    break;
                case TxParameterID::PeerID:
                    deserialize(txDescription.m_peerId, parameter.m_value);
                    break;
                case TxParameterID::MyID:
                    deserialize(txDescription.m_myId, parameter.m_value);
                    break;
                case TxParameterID::CreateTime:
                    deserialize(txDescription.m_createTime, parameter.m_value);
                    break;
                case TxParameterID::IsSender:
                    deserialize(txDescription.m_sender, parameter.m_value);
                    break;
                case TxParameterID::Message:
                    deserialize(txDescription.m_message, parameter.m_value);
                    break;
                case TxParameterID::Change:
                    deserialize(txDescription.m_change, parameter.m_value);
                    break;
                case TxParameterID::ModifyTime:
                    deserialize(txDescription.m_modifyTime, parameter.m_value);
                    break;
                case TxParameterID::Status:
                    deserialize(txDescription.m_status, parameter.m_value);
                    break;
                case TxParameterID::KernelID:
                    deserialize(txDescription.m_kernelID, parameter.m_value);
                    break;
                case TxParameterID::FailureReason:
                    deserialize(txDescription.m_failureReason, parameter.m_value);
                    break;
                case TxParameterID::IsSelfTx:
                    deserialize(txDescription.m_selfTx, parameter.m_value);
                    break;
                default:
                    break; // suppress warning
                }
            }

        }

        if (std::includes(gottenParams.begin(), gottenParams.end(), m_mandatoryTxParams.begin(), m_mandatoryTxParams.end()))
        {
            return txDescription;
        }

        return boost::optional<TxDescription>{};
    }

    void WalletDB::saveTx(const TxDescription& p)
    {
        ChangeAction action = ChangeAction::Added;

        storage::setTxParameter(*this, p.m_txId, TxParameterID::TransactionType, p.m_txType, false);
        storage::setTxParameter(*this, p.m_txId, TxParameterID::Amount, p.m_amount, false);
        storage::setTxParameter(*this, p.m_txId, TxParameterID::Fee, p.m_fee, false);
        storage::setTxParameter(*this, p.m_txId, TxParameterID::Change, p.m_change, false);
        if (p.m_minHeight)
        {
            storage::setTxParameter(*this, p.m_txId, TxParameterID::MinHeight, p.m_minHeight, false);
        }
        storage::setTxParameter(*this, p.m_txId, TxParameterID::PeerID, p.m_peerId, false);
        storage::setTxParameter(*this, p.m_txId, TxParameterID::MyID, p.m_myId, false);
        storage::setTxParameter(*this, p.m_txId, TxParameterID::Message, p.m_message, false);
        storage::setTxParameter(*this, p.m_txId, TxParameterID::CreateTime, p.m_createTime, false);
        storage::setTxParameter(*this, p.m_txId, TxParameterID::ModifyTime, p.m_modifyTime, false);
        storage::setTxParameter(*this, p.m_txId, TxParameterID::IsSender, p.m_sender, false);
        storage::setTxParameter(*this, p.m_txId, TxParameterID::Status, p.m_status, false);
        storage::setTxParameter(*this, p.m_txId, TxParameterID::IsSelfTx, p.m_selfTx, false);

        // notify only when full TX saved
        notifyTransactionChanged(action, {p});
    }

    void WalletDB::deleteTx(const TxID& txId)
    {
        auto tx = getTx(txId);
        if (tx.is_initialized())
        {
            const char* req = "DELETE FROM " TX_PARAMS_NAME " WHERE txID=?1 AND paramID!=?2;";
            sqlite::Statement stm(this, req);

            stm.bind(1, txId);
            stm.bind(2, TxParameterID::TransactionType);

            stm.step();
            deleteParametersFromCache(txId);
            notifyTransactionChanged(ChangeAction::Removed, { *tx });
        }
    }

    void WalletDB::rollbackTx(const TxID& txId)
    {
        {
            const char* req = "UPDATE " STORAGE_NAME " SET spentTxId=NULL WHERE spentTxId=?1;";
            sqlite::Statement stm(this, req);
            stm.bind(1, txId);
            stm.step();
        }
        {
            const char* req = "DELETE FROM " STORAGE_NAME " WHERE createTxId=?1 AND confirmHeight=?2;";
            sqlite::Statement stm(this, req);
            stm.bind(1, txId);
            stm.bind(2, MaxHeight);
            stm.step();
        }
        notifyCoinsChanged();
    }

    void WalletDB::deleteCoinsCreatedByTx(const TxID& txId)
    {
        {
            const char* req = "DELETE FROM " STORAGE_NAME " WHERE createTxId=?1 AND confirmHeight=?2;";
            sqlite::Statement stm(this, req);
            stm.bind(1, txId);
            stm.bind(2, MaxHeight);
            stm.step();
        }
        notifyCoinsChanged();
    }

    boost::optional<WalletAddress> WalletDB::getAddress(const WalletID& id) const
    {
        if (auto it = m_AddressesCache.find(id); it != m_AddressesCache.end())
        {
            return it->second;
        }
        const char* req = "SELECT * FROM " ADDRESSES_NAME " WHERE walletID=?1;";
        sqlite::Statement stm(this, req);

        stm.bind(1, id);

        if (stm.step())
        {
            WalletAddress address = {};
            int colIdx = 0;
            ENUM_ADDRESS_FIELDS(STM_GET_LIST, NOSEP, address);
            insertAddressToCache(id, address);
            return address;
        }
        insertAddressToCache(id, boost::optional<WalletAddress>());
        return boost::optional<WalletAddress>();
    }

    std::vector<WalletAddress> WalletDB::getAddresses(bool own) const
    {
        vector<WalletAddress> res;
        const char* req = "SELECT * FROM " ADDRESSES_NAME " ORDER BY createTime DESC;";
        sqlite::Statement stm(this, req);

        while (stm.step())
        {
            auto& a = res.emplace_back();
            int colIdx = 0;
            ENUM_ADDRESS_FIELDS(STM_GET_LIST, NOSEP, a);

            if ((!a.m_OwnID) == own)
                res.pop_back(); // akward, but ok
        }
        return res;
    }

    void WalletDB::saveAddress(const WalletAddress& address)
    {
        ChangeAction action = ChangeAction::Added;
        {
            const char* selectReq = "SELECT * FROM " ADDRESSES_NAME " WHERE walletID=?1;";
            sqlite::Statement stm2(this, selectReq);
            stm2.bind(1, address.m_walletID);

            if (stm2.step())
            {
                const char* updateReq = "UPDATE " ADDRESSES_NAME " SET label=?2, category=?3, duration=?4, createTime=?5 WHERE walletID=?1;";
                sqlite::Statement stm(this, updateReq);

                stm.bind(1, address.m_walletID);
                stm.bind(2, address.m_label);
                stm.bind(3, address.m_category);
                stm.bind(4, address.m_duration);
                stm.bind(5, address.m_createTime);
                stm.step();

                action = ChangeAction::Updated;
            }
            else
            {
                const char* insertReq = "INSERT INTO " ADDRESSES_NAME " (" ENUM_ADDRESS_FIELDS(LIST, COMMA, ) ") VALUES(" ENUM_ADDRESS_FIELDS(BIND_LIST, COMMA, ) ");";
                sqlite::Statement stm(this, insertReq);
                int colIdx = 0;
                ENUM_ADDRESS_FIELDS(STM_BIND_LIST, NOSEP, address);
                stm.step();
            }
        }

        insertAddressToCache(address.m_walletID, address);
        notifyAddressChanged(action, { address });
    }

    void WalletDB::deleteAddress(const WalletID& id)
    {
        auto address = getAddress(id);
        if (address)
        {
            const char* req = "DELETE FROM " ADDRESSES_NAME " WHERE walletID=?1;";
            sqlite::Statement stm(this, req);

            stm.bind(1, id);

            stm.step();

            deleteAddressFromCache(id);

            notifyAddressChanged(ChangeAction::Removed, {*address});
        }
    }

    void WalletDB::insertAddressToCache(const WalletID& id, const boost::optional<WalletAddress>& address) const
    {
        m_AddressesCache[id] = address;
    }

    void WalletDB::deleteAddressFromCache(const WalletID& id)
    {
        m_AddressesCache.erase(id);
    }

    void WalletDB::Subscribe(IWalletDbObserver* observer)
    {
        assert(std::find(m_subscribers.begin(), m_subscribers.end(), observer) == m_subscribers.end());

        m_subscribers.push_back(observer);
    }

    void WalletDB::Unsubscribe(IWalletDbObserver* observer)
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

    bool WalletDB::setTxParameter(const TxID& txID, SubTxID subTxID, TxParameterID paramID, const ByteBuffer& blob, bool shouldNotifyAboutChanges)
    {
        if (auto txIter = m_TxParametersCache.find(txID); txIter != m_TxParametersCache.end())
        {
            if (auto subTxIter = txIter->second.find(subTxID); subTxIter != txIter->second.end())
            {
                if (auto pit = subTxIter->second.find(paramID); pit != subTxIter->second.end())
                {
                    if (pit->second && blob == *(pit->second))
                    {
                        return false;
                    }
                }
            }
        }

        bool hasTx = hasTransaction(txID);
        {
            sqlite::Statement stm(this, "SELECT * FROM " TX_PARAMS_NAME " WHERE txID=?1 AND subTxID=?2 AND paramID=?3;");

            stm.bind(1, txID);
            stm.bind(2, subTxID);
            stm.bind(3, paramID);

            if (stm.step())
            {
                // already set
                if (paramID < TxParameterID::PrivateFirstParam)
                {
                    return false;
                }

                sqlite::Statement stm2(this, "UPDATE " TX_PARAMS_NAME  " SET value = ?4 WHERE txID = ?1 AND subTxID=?2 AND paramID = ?3;");
                stm2.bind(1, txID);
                stm2.bind(2, subTxID);
                stm2.bind(3, paramID);
                stm2.bind(4, blob);
                stm2.step();

                if (shouldNotifyAboutChanges)
                {
                    auto tx = getTx(txID);
                    if (tx.is_initialized())
                    {
                        notifyTransactionChanged(ChangeAction::Updated, { *tx });
                    }
                }
                insertParameterToCache(txID, subTxID, paramID, blob);
                return true;
            }
        }
        
        sqlite::Statement stm(this, "INSERT INTO " TX_PARAMS_NAME " (" ENUM_TX_PARAMS_FIELDS(LIST, COMMA, ) ") VALUES(" ENUM_TX_PARAMS_FIELDS(BIND_LIST, COMMA, ) ");");
        TxParameter parameter;
        parameter.m_txID = txID;
        parameter.m_subTxID = subTxID;
        parameter.m_paramID = static_cast<int>(paramID);
        parameter.m_value = blob;
        int colIdx = 0;
        ENUM_TX_PARAMS_FIELDS(STM_BIND_LIST, NOSEP, parameter);
        stm.step();
        if (shouldNotifyAboutChanges)
        {
            auto tx = getTx(txID);
            if (tx.is_initialized())
            {
                notifyTransactionChanged(hasTx ? ChangeAction::Updated : ChangeAction::Added, { *tx });
            }
        }
        insertParameterToCache(txID, subTxID, paramID, blob);
        return true;
    }

    bool WalletDB::getTxParameter(const TxID& txID, SubTxID subTxID, TxParameterID paramID, ByteBuffer& blob) const
    {
        if (auto txIter = m_TxParametersCache.find(txID); txIter != m_TxParametersCache.end())
        {
            if (auto subTxIter = txIter->second.find(subTxID); subTxIter != txIter->second.end())
            {
                if (auto pit = subTxIter->second.find(paramID); pit != subTxIter->second.end())
                {
                    if (pit->second)
                    {
                        blob = *(pit->second);
                        return true;
                    }
                    return false;
                }
            }
        }

        sqlite::Statement stm(this, "SELECT * FROM " TX_PARAMS_NAME " WHERE txID=?1 AND subTxID=?2 AND paramID=?3;");

        stm.bind(1, txID);
        stm.bind(2, subTxID);
        stm.bind(3, paramID);

        if (stm.step())
        {
            TxParameter parameter = {};
            int colIdx = 0;
            ENUM_TX_PARAMS_FIELDS(STM_GET_LIST, NOSEP, parameter);
            blob = move(parameter.m_value);
            insertParameterToCache(txID, subTxID, paramID, blob);
            return true;
        }
        insertParameterToCache(txID, subTxID, paramID, boost::optional<ByteBuffer>());
        return false;
    }

    auto WalletDB::getAllTxParameters() const -> std::vector<TxParameter>
    {
        sqlite::Statement stm(this, "SELECT * FROM " TX_PARAMS_NAME ";");
        std::vector<TxParameter> res;
        while (stm.step())
        {
            auto& p = res.emplace_back();
            int colIdx = 0;
            ENUM_TX_PARAMS_FIELDS(STM_GET_LIST, NOSEP, p);
            insertParameterToCache(
				p.m_txID,
				static_cast<SubTxID>(p.m_subTxID),
				static_cast<TxParameterID>(p.m_paramID),
				p.m_value);
        }
        return res;
    }

    void WalletDB::insertParameterToCache(const TxID& txID, SubTxID subTxID, TxParameterID paramID, const boost::optional<ByteBuffer>& blob) const
    {
        m_TxParametersCache[txID][subTxID][paramID] = blob;
    }

    void WalletDB::deleteParametersFromCache(const TxID& txID)
    {
        m_TxParametersCache.erase(txID);
    }

    bool WalletDB::hasTransaction(const TxID& txID) const
    {
        ByteBuffer blob;
        for (const auto& paramID : m_mandatoryTxParams)
        {
            if (!getTxParameter(txID, kDefaultSubTxID, paramID, blob))
            {
                return false;
            }
        }
        return true;
    }

    void WalletDB::flushDB()
    {
        if (m_IsFlushPending)
        {
            assert(m_FlushTimer);
            m_FlushTimer->cancel();
            onFlushTimer();
        }
    }

    void WalletDB::rollbackDB()
    {
        if (m_IsFlushPending)
        {
            assert(m_FlushTimer);
            m_FlushTimer->cancel();
            m_IsFlushPending = false;
            if (m_DbTransaction)
            {
                m_DbTransaction->rollback();
                m_DbTransaction.reset();
            }
        }
    }

    void WalletDB::onModified()
    {
        if (!m_IsFlushPending)
        {
            if (!m_FlushTimer)
            {
                m_FlushTimer = io::Timer::create(*m_Reactor);
            }
            m_FlushTimer->start(50, false, BIND_THIS_MEMFN(onFlushTimer));
            m_IsFlushPending = true;
        }
    }

    void WalletDB::onFlushTimer()
    {
        m_IsFlushPending = false;
        if (m_DbTransaction)
        {
            m_DbTransaction->commit();
            m_DbTransaction.reset();
        }
    }

    void WalletDB::onPrepareToModify()
    {
        if (!m_DbTransaction)
        {
            m_DbTransaction.reset(new sqlite::Transaction(_db));
        }
    }

    void WalletDB::notifyCoinsChanged()
    {
        for (auto sub : m_subscribers) sub->onCoinsChanged();
    }

    void WalletDB::notifyTransactionChanged(ChangeAction action, const vector<TxDescription>& items)
    {
        for (auto sub : m_subscribers)
        {
            sub->onTransactionChanged(action, items);
        }
    }

    void WalletDB::notifySystemStateChanged(const Block::SystemState::ID& stateID)
    {
        for (auto sub : m_subscribers) sub->onSystemStateChanged(stateID);
    }

    void WalletDB::notifyAddressChanged(ChangeAction action, const vector<WalletAddress>& items)
    {
        for (auto sub : m_subscribers)
        {
            sub->onAddressChanged(action, items);
        }
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
            const Height hMaxBacklog = Rules::get().MaxRollback * 2; // can actually be more

            if (s.m_Height > hMaxBacklog)
            {
                const char* req = "DELETE FROM " TblStates " WHERE " TblStates_Height "<=?";
                sqlite::Statement stm(this, req);
                stm.bind(1, s.m_Height - hMaxBacklog);
                stm.step();

            }
        }
    }

    bool WalletDB::lockCoins(const CoinIDList& list, uint64_t session)
    {
        auto coins = getCoinsByID(list);
        for (auto& coin : coins)
        {
            if (coin.m_sessionId == 0)
            {
                coin.m_sessionId = session;
            }
            else
            {
                // error, coin already locked
                return false;
            }
        }

        saveCoins(coins);

        return !coins.empty();
    }

    bool WalletDB::unlockCoins(uint64_t session)
    {
        const char* req = "UPDATE " STORAGE_NAME " SET sessionId=0 WHERE sessionId=?1;";
        sqlite::Statement stm(this, req);

        stm.bind(1, session);

        stm.step();

        return sqlite3_changes(_db) > 0;
    }

    CoinIDList WalletDB::getLockedCoins(uint64_t session) const
    {
        const char* req = "SELECT " STORAGE_FIELDS " FROM " STORAGE_NAME " WHERE sessionId=?1;";
        sqlite::Statement stm(this, req);

        stm.bind(1, session);

        CoinIDList list;

        while (stm.step())
        {
            Coin coin;

            int colIdx = 0;
            ENUM_ALL_STORAGE_FIELDS(STM_GET_LIST, NOSEP, coin);

            list.push_back(coin.m_ID);
        }

        return list;
    }

    std::vector<OutgoingWalletMessage> WalletDB::getWalletMessages() const
    {
        std::vector<OutgoingWalletMessage> messages;
        sqlite::Statement stm(this, "SELECT * FROM " WALLET_MESSAGE_NAME " ;");
        while (stm.step())
        {
            auto& message = messages.emplace_back();
            int colIdx = 0;
            ENUM_WALLET_MESSAGE_FIELDS(STM_GET_LIST, NOSEP, message);
        }
        return messages;
    }

    uint64_t WalletDB::saveWalletMessage(const OutgoingWalletMessage& message)
    {
        const char* req = "INSERT INTO " WALLET_MESSAGE_NAME " (PeerID, Message) VALUES(?,?)";
        sqlite::Statement stm(this, req);
        stm.bind(1, message.m_PeerID);
        stm.bind(2, message.m_Message);

        stm.step();

        return sqlite3_last_insert_rowid(_db);
    }

    void WalletDB::deleteWalletMessage(uint64_t id)
    {
        sqlite::Statement stm(this, "DELETE FROM " WALLET_MESSAGE_NAME " WHERE ID == ?1;");
        stm.bind(1, id);
        stm.step();
    }

    std::vector<IncomingWalletMessage> WalletDB::getIncomingWalletMessages() const
    {
        std::vector<IncomingWalletMessage> messages;
        sqlite::Statement stm(this, "SELECT * FROM " INCOMING_WALLET_MESSAGE_NAME " ;");
        while (stm.step())
        {
            auto& message = messages.emplace_back();
            int colIdx = 0;
            ENUM_INCOMING_WALLET_MESSAGE_FIELDS(STM_GET_LIST, NOSEP, message);
        }
        return messages;
    }

    uint64_t WalletDB::saveIncomingWalletMessage(BbsChannel channel, const ByteBuffer& message)
    {
        const char* req = "INSERT INTO " INCOMING_WALLET_MESSAGE_NAME " (Channel, Message) VALUES(?,?);";
        sqlite::Statement stm(this, req);
        stm.bind(1, channel);
        stm.bind(2, message);

        stm.step();

        return sqlite3_last_insert_rowid(_db);
    }

    void WalletDB::deleteIncomingWalletMessage(uint64_t id)
    {
        sqlite::Statement stm(this, "DELETE FROM " INCOMING_WALLET_MESSAGE_NAME " WHERE ID == ?1;");
        stm.bind(1, id);
        stm.step();
    }

    bool WalletDB::History::Enum(IWalker& w, const Height* pBelow)
    {
        const char* req = pBelow ?
            "SELECT " TblStates_Hdr " FROM " TblStates " WHERE " TblStates_Height "<? ORDER BY " TblStates_Height " DESC;" :
            "SELECT " TblStates_Hdr " FROM " TblStates " ORDER BY " TblStates_Height " DESC;";

        sqlite::Statement stm(&get_ParentObj(), req);

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

        sqlite::Statement stm(&get_ParentObj(), req);
        stm.bind(1, h);

        if (!stm.step())
            return false;

        stm.get(0, s);
        return true;
    }

    void WalletDB::History::AddStates(const Block::SystemState::Full* pS, size_t nCount)
    {
        const char* req = "INSERT OR REPLACE INTO " TblStates " (" TblStates_Height "," TblStates_Hdr ") VALUES(?,?)";
        sqlite::Statement stm(&get_ParentObj(), req);

        for (size_t i = 0; i < nCount; i++)
        {
            if (i)
                stm.Reset();

            stm.bind(1, pS[i].m_Height);
            stm.bind(2, pS[i]);
            stm.step();
        }
    }

    void WalletDB::History::DeleteFrom(Height h)
    {
        const char* req = "DELETE FROM " TblStates " WHERE " TblStates_Height ">=?";
        sqlite::Statement stm(&get_ParentObj(), req);
        stm.bind(1, h);
        stm.step();
    }

    namespace storage
    {
        const char g_szPaymentProofRequired[] = "payment_proof_required";

        bool getTxParameter(const IWalletDB& db, const TxID& txID, SubTxID subTxID, TxParameterID paramID, ECC::Point::Native& value)
        {
            ECC::Point pt;
            if (getTxParameter(db, txID, subTxID, paramID, pt))
            {
                return value.Import(pt);
            }
            return false;
        }

        bool getTxParameter(const IWalletDB& db, const TxID& txID, SubTxID subTxID, TxParameterID paramID, ECC::Scalar::Native& value)
        {
            ECC::Scalar s;
            if (getTxParameter(db, txID, subTxID, paramID, s))
            {
                value.Import(s);
                return true;
            }
            return false;
        }

        bool getTxParameter(const IWalletDB& db, const TxID& txID, SubTxID subTxID, TxParameterID paramID, ByteBuffer& value)
        {
            return db.getTxParameter(txID, subTxID, paramID, value);
        }

        bool getTxParameter(const IWalletDB& db, const TxID& txID, TxParameterID paramID, ECC::Point::Native& value)
        {
            return getTxParameter(db, txID, kDefaultSubTxID, paramID, value);
        }

        bool getTxParameter(const IWalletDB& db, const TxID& txID, TxParameterID paramID, ByteBuffer& value)
        {
            return getTxParameter(db, txID, kDefaultSubTxID, paramID, value);
        }

        bool getTxParameter(const IWalletDB& db, const TxID& txID, TxParameterID paramID, ECC::Scalar::Native& value)
        {
            return getTxParameter(db, txID, kDefaultSubTxID, paramID, value);
        }

        bool setTxParameter(IWalletDB& db, const TxID& txID, SubTxID subTxID, TxParameterID paramID,
            const ECC::Point::Native& value, bool shouldNotifyAboutChanges)
        {
            ECC::Point pt;
            if (value.Export(pt))
            {
                return setTxParameter(db, txID, subTxID, paramID, pt, shouldNotifyAboutChanges);
            }
            return false;
        }

        bool setTxParameter(IWalletDB& db, const TxID& txID, SubTxID subTxID, TxParameterID paramID,
            const ECC::Scalar::Native& value, bool shouldNotifyAboutChanges)
        {
            ECC::Scalar s;
            value.Export(s);
            return setTxParameter(db, txID, subTxID, paramID, s, shouldNotifyAboutChanges);
        }

        bool setTxParameter(IWalletDB& db, const TxID& txID, SubTxID subTxID, TxParameterID paramID,
            const ByteBuffer& value, bool shouldNotifyAboutChanges)
        {
            return db.setTxParameter(txID, subTxID, paramID, value, shouldNotifyAboutChanges);
        }

        bool setTxParameter(IWalletDB& db, const TxID& txID, TxParameterID paramID, const ECC::Point::Native& value, bool shouldNotifyAboutChanges)
        {
            return setTxParameter(db, txID, kDefaultSubTxID, paramID, value, shouldNotifyAboutChanges);
        }

        bool setTxParameter(IWalletDB& db, const TxID& txID, TxParameterID paramID, const ECC::Scalar::Native& value, bool shouldNotifyAboutChanges)
        {
            return setTxParameter(db, txID, kDefaultSubTxID, paramID, value, shouldNotifyAboutChanges);
        }

        bool setTxParameter(IWalletDB& db, const TxID& txID, TxParameterID paramID, const ByteBuffer& value, bool shouldNotifyAboutChanges)
        {
            return setTxParameter(db, txID, kDefaultSubTxID, paramID, value, shouldNotifyAboutChanges);
        }

        bool changeAddressExpiration(IWalletDB& walletDB, const WalletID& walletID, WalletAddress::ExpirationStatus status)
        {
            if (walletID != Zero)
            {
                auto address = walletDB.getAddress(walletID);

                if (!address.is_initialized())
                {
                    LOG_INFO() << "Address " << to_string(walletID) << "is absent in wallet";
                    return false;
                }

                address->setExpiration(status);
                walletDB.saveAddress(*address);
            }
            else
            {
                for (auto& address : walletDB.getAddresses(true))
                {
                    address.setExpiration(status);
                    walletDB.saveAddress(address);
                }
            }
            return true;
        }

        void Totals::Init(IWalletDB& walletDB)
        {
            ZeroObject(*this);

            walletDB.visitCoins([this](const Coin& c)->bool
            {
                const Amount& v = c.m_ID.m_Value; // alias
                switch (c.m_status)
                {
                case Coin::Status::Available:
                    Avail += v;
                    Unspent += v;

                    switch (c.m_ID.m_Type)
                    {
                    case Key::Type::Coinbase: AvailCoinbase += v; break;
                    case Key::Type::Comission: AvailFee += v; break;
                    default: // suppress warning
                        break;
                    }

                    break;

                case Coin::Status::Maturing:
                    Maturing += v;
                    Unspent += v;
                    break;

                case Coin::Status::Incoming:
                {
                    Incoming += v;
                    if (c.m_ID.m_Type == Key::Type::Change)
                    {
                        ReceivingChange += v;
                    }
                    else
                    {
                        ReceivingIncoming += v;
                    }

                    break;
                }
                case Coin::Status::Outgoing: Outgoing += v; break;
                case Coin::Status::Unavailable: Unavail += v; break;

                default: // suppress warning
                    break;
                }

                switch (c.m_ID.m_Type)
                {
                case Key::Type::Coinbase: Coinbase += v; break;
                case Key::Type::Comission: Fee += v; break;
                default: // suppress warning
                    break;
                }

                return true;
            });
        }

        WalletAddress createAddress(IWalletDB& walletDB, IPrivateKeyKeeper::Ptr keyKeeper)
        {
            WalletAddress newAddress;
            newAddress.m_createTime = beam::getTimestamp();
            newAddress.m_OwnID = walletDB.AllocateKidRange(1);
            newAddress.m_walletID = generateWalletIDFromIndex(keyKeeper, newAddress.m_OwnID);

            return newAddress;
        }

        WalletID generateWalletIDFromIndex(IPrivateKeyKeeper::Ptr keyKeeper, uint64_t ownID)
        {
            if (!keyKeeper->get_SbbsKdf())
            {
                throw CannotGenerateSecretException();
            }
            WalletID walletID(Zero);

            ECC::Scalar::Native sk;

            keyKeeper->get_SbbsKdf()->DeriveKey(sk, Key::ID(ownID, Key::Type::Bbs));

            proto::Sk2Pk(walletID.m_Pk, sk);

            // derive the channel from the address
            BbsChannel ch;
            walletID.m_Pk.ExportWord<0>(ch);
            ch %= proto::Bbs::s_MaxChannels;

            walletID.m_Channel = ch;

            return walletID;
        }

        void DeduceStatus(const IWalletDB& walletDB, Coin& c, Height hTop)
        {
            c.m_status = GetCoinStatus(walletDB, c, hTop);
        }

        bool IsOngoingTx(const IWalletDB& walletDB, const boost::optional<TxID>& txID)
        {
            if (!txID)
                return false;

            TxStatus txStatus;
            if (getTxParameter(walletDB, txID.get(), TxParameterID::Status, txStatus))
            {
                switch (txStatus)
                {
                case TxStatus::Canceled:
                case TxStatus::Failed:
                case TxStatus::Completed:
                    break;

                default:
                    return true;
                }
            }

            return false;
        }

        Coin::Status GetCoinStatus(const IWalletDB& walletDB, const Coin& c, Height hTop)
        {
            if (c.m_spentHeight != MaxHeight)
                return Coin::Status::Spent;

            if (c.m_confirmHeight != MaxHeight)
            {
                if (c.m_maturity > hTop)
                    return Coin::Status::Maturing;

                if (IsOngoingTx(walletDB, c.m_spentTxId))
                    return Coin::Status::Outgoing;

                return Coin::Status::Available;
            }

            if (IsOngoingTx(walletDB, c.m_createTxId))
                return Coin::Status::Incoming;

            return Coin::Status::Unavailable;
        }

        using nlohmann::json;

        namespace
        {
            namespace Fields
            {
                const string OwnAddresses = "OwnAddresses";
                const string Contacts = "Contacts";
                const string TransactionParameters = "TransactionParameters";
                const string Category = "Category";
                const string WalletID = "WalletID";
                const string Index = "Index";
                const string Label = "Label";
                const string CreationTime = "CreationTime";
                const string Duration = "Duration";
                const string TransactionId = "TransactionId";
                const string SubTransactionId = "SubTransactionId";
                const string ParameterId = "ParameterId";
                const string Value = "Value";
            }
            
            bool ImportAddressesFromJson(IWalletDB& db, IPrivateKeyKeeper::Ptr keyKeeper, const json& obj, const string& nodeName)
            {
                if (obj.find(nodeName) == obj.end())
                {
                    return true;
                }

                for (const auto& jsonAddress : obj[nodeName])
                {
                    WalletAddress address;
                    if (address.m_walletID.FromHex(jsonAddress[Fields::WalletID]))
                    {
                        address.m_OwnID = jsonAddress[Fields::Index];
                        if (address.m_OwnID == 0 || address.m_walletID == generateWalletIDFromIndex(keyKeeper, address.m_OwnID))
                        {
                            //{ "SubIndex", 0 },
                            address.m_label = jsonAddress[Fields::Label];
                            auto creationTime = jsonAddress[Fields::CreationTime];
                            auto currentTime = beam::getTimestamp();
                            if (currentTime >= creationTime)
                            {
                                address.m_createTime = creationTime;
                                address.m_duration = jsonAddress[Fields::Duration];
                            }
                            else
                            {
                                address.m_createTime = currentTime;
                                address.m_duration = WalletAddress::AddressExpiration24h;
                            }
                            if (jsonAddress.find(Fields::Category) != jsonAddress.end()) // for compatibility with older export
                            {
                                address.m_category = jsonAddress[Fields::Category];
                            }
                            db.saveAddress(address);

                            LOG_INFO() << "The address [" << jsonAddress[Fields::WalletID] << "] has been successfully imported.";
                            continue;
                        }
                    }

                    LOG_INFO() << "The address [" << jsonAddress[Fields::WalletID] << "] has NOT been imported. Wrong address.";
                }
                return true;
            }

            bool ImportAddressesFromJson(IWalletDB& db, IPrivateKeyKeeper::Ptr keyKeeper, const json& obj)
            {
                return ImportAddressesFromJson(db, keyKeeper, obj, Fields::OwnAddresses);
            }

            bool ImportContactsFromJson(IWalletDB& db, IPrivateKeyKeeper::Ptr keyKeeper, const json& obj)
            {
                return ImportAddressesFromJson(db, keyKeeper, obj, Fields::Contacts);
            }

            bool ImportTransactionsFromJson(IWalletDB& db, IPrivateKeyKeeper::Ptr keyKeeper, const json& obj)
            {
                if (obj.find(Fields::TransactionParameters) == obj.end())
                {
                    return true;
                }

                std::unordered_map<
                    TxID,
                    std::unordered_map<
                    TxParameterID,
                    TxParameter>
                > importedTransactionsMap;
                for (const auto& jsonTxParameter : obj[Fields::TransactionParameters])
                {
                    TxParameter txParameter;
                    txParameter.m_txID = jsonTxParameter[Fields::TransactionId];
                    txParameter.m_subTxID = jsonTxParameter[Fields::SubTransactionId];
                    txParameter.m_paramID = jsonTxParameter[Fields::ParameterId];
                    for (const auto& v : jsonTxParameter[Fields::Value])
                    {
                        txParameter.m_value.push_back(v);
                    }
                    importedTransactionsMap[txParameter.m_txID].emplace(static_cast<TxParameterID>(txParameter.m_paramID), txParameter);
                }
                for (const auto& txPair : importedTransactionsMap)
                {
                    const auto& paramsMap = txPair.second;
                    WalletID wid;
                    uint64_t myAddrId = 0;

                    //paramsMap
                    if (auto idIt = paramsMap.find(TxParameterID::MyID);
                        idIt == paramsMap.end() ||
                        !wid.FromBuf(idIt->second.m_value) ||
                        !wid.IsValid())
                    {
                        LOG_ERROR() << "Transaction " << txPair.first << " was not imported. Invalid myID parameter";
                        continue;
                    }

                    auto waddr = db.getAddress(wid);
                    if (waddr && (waddr->m_OwnID == 0 || wid != generateWalletIDFromIndex(keyKeeper, waddr->m_OwnID)))
                    {
                        LOG_ERROR() << "Transaction " << txPair.first << " was not imported. Invalid address parameter";
                        continue;
                    }

                    auto addressIt = paramsMap.find(TxParameterID::MyAddressID);
                    if (addressIt != paramsMap.end() && (!fromByteBuffer(addressIt->second.m_value, myAddrId) ||
                        wid != generateWalletIDFromIndex(keyKeeper, myAddrId)))
                    {
                        LOG_ERROR() << "Transaction " << txPair.first << " was not imported. Invalid MyAddressID parameter";
                        continue;
                    }
                    
                    if (!waddr && addressIt == paramsMap.end())
                    {
                        LOG_WARNING() << "Transaction " << txPair.first << ". Cannot check imported address";
                    }
                    
                    for (const auto& paramPair : paramsMap)
                    {
                        const auto& p = paramPair.second;
                        db.setTxParameter(p.m_txID,
                            static_cast<SubTxID>(p.m_subTxID),
                            paramPair.first,
                            p.m_value,
                            true);
                    }
                    LOG_INFO() << "Transaction " << txPair.first << " was imported.";

                }
                return true;
            }

            json ExportAddressesToJson(const IWalletDB& db, bool own)
            {
                json addresses = json::array();
                for (const auto& address : db.getAddresses(own))
                {
                    addresses.push_back(
                        json
                        {
                            {Fields::Index, address.m_OwnID},
                            {"SubIndex", 0},
                            {Fields::WalletID, to_string(address.m_walletID)},
                            {Fields::Label, address.m_label},
                            {Fields::CreationTime, address.m_createTime},
                            {Fields::Duration, address.m_duration},
                            {Fields::Category, address.m_category}
                        }
                    );
                }
                return addresses;
            }


            json ExportTransactionsToJson(const IWalletDB& db)
            {
                json txParams = json::array();
                for (const auto& p : db.getAllTxParameters())
                {
                    txParams.push_back(
                        json
                        {
                            {Fields::TransactionId, p.m_txID},
                            {Fields::SubTransactionId, p.m_subTxID},
                            {Fields::ParameterId, p.m_paramID},
                            {Fields::Value, p.m_value}
                        }
                    );
                }
                return txParams;
            }
        }

        string ExportDataToJson(const IWalletDB& db)
        {
            auto res = json
            {
                {Fields::OwnAddresses, ExportAddressesToJson(db, true)},
                {Fields::Contacts, ExportAddressesToJson(db, false)},
                {Fields::TransactionParameters, ExportTransactionsToJson(db)}
            };
            return res.dump();
        }

        bool ImportDataFromJson(IWalletDB& db, IPrivateKeyKeeper::Ptr keyKeeper, const char* data, size_t size)
        {
            try
            {
                json obj = json::parse(data, data + size);
                return ImportAddressesFromJson(db, keyKeeper, obj) 
                    && ImportContactsFromJson(db, keyKeeper, obj)
                    && ImportTransactionsFromJson(db, keyKeeper, obj);
            }
            catch (const nlohmann::detail::exception& e)
            {
                LOG_ERROR() << "json parse: " << e.what() << "\n" << std::string(data, data + (size > 1024 ? 1024 : size));
            }
            return false;
        }

        PaymentInfo::PaymentInfo()
        {
            Reset();
        }

        bool PaymentInfo::IsValid() const
        {
            wallet::PaymentConfirmation pc;
            pc.m_Value = m_Amount;
            pc.m_KernelID = m_KernelID;
            pc.m_Signature = m_Signature;
            pc.m_Sender = m_Sender.m_Pk;
            return pc.IsValid(m_Receiver.m_Pk);
        }

        std::string PaymentInfo::to_string() const
        {
            std::ostringstream s;
            s
                << "Sender: " << std::to_string(m_Sender) << std::endl
                << "Receiver: " << std::to_string(m_Receiver) << std::endl
                << "Amount: " << PrintableAmount(m_Amount) << std::endl
                << "KernelID: " << std::to_string(m_KernelID) << std::endl;

            return s.str();
        }

        void PaymentInfo::Reset()
        {
            ZeroObject(*this);
        }

        PaymentInfo PaymentInfo::FromByteBuffer(const ByteBuffer& data)
        {
            PaymentInfo pi;
            if (!data.empty())
            {
                Deserializer der;
                der.reset(data);
                der & pi;
                if (der.bytes_left() > 0)
                {
                    throw std::runtime_error("Invalid data buffer");
                }
            }
            return pi;
        }

        std::string TxDetailsInfo(const IWalletDB::Ptr& walletDB, const TxID& txID)
        {
            PaymentInfo pi;
            auto tx = walletDB->getTx(txID);

            bool bSuccess =
                storage::getTxParameter(*walletDB,
                                        txID,
                                        tx->m_sender
                                            ? TxParameterID::PeerID
                                            : TxParameterID::MyID,
                                        pi.m_Receiver) &&
                storage::getTxParameter(*walletDB,
                                        txID,
                                        tx->m_sender
                                            ? TxParameterID::MyID
                                            : TxParameterID::PeerID,
                                        pi.m_Sender) &&
                storage::getTxParameter(
                    *walletDB, txID, TxParameterID::KernelID, pi.m_KernelID) &&
                storage::getTxParameter(
                    *walletDB, txID, TxParameterID::Amount, pi.m_Amount);

            if (bSuccess)
            {
                return pi.to_string();
            }

            LOG_WARNING() << "Can't get transaction details";
            return "";

        }

        ByteBuffer ExportPaymentProof(const IWalletDB& walletDB, const TxID& txID)
        {
            PaymentInfo pi;
            uint64_t nAddrOwnID;

            bool bSuccess =
                storage::getTxParameter(walletDB, txID, TxParameterID::PeerID, pi.m_Receiver) &&
                storage::getTxParameter(walletDB, txID, TxParameterID::MyID, pi.m_Sender) &&
                storage::getTxParameter(walletDB, txID, TxParameterID::KernelID, pi.m_KernelID) &&
                storage::getTxParameter(walletDB, txID, TxParameterID::Amount, pi.m_Amount) &&
                storage::getTxParameter(walletDB, txID, TxParameterID::PaymentConfirmation, pi.m_Signature) &&
                storage::getTxParameter(walletDB, txID, TxParameterID::MyAddressID, nAddrOwnID);

            if (bSuccess)
            {
                LOG_INFO() << "Payment tx details:\n" << pi.to_string();
                LOG_INFO() << "Sender address own ID: " << nAddrOwnID;

                Serializer ser;
                ser & pi;

                auto res = ser.buffer();
                return ByteBuffer(res.first, res.first + res.second);
            }
            else
            {
                LOG_WARNING() << "No payment confirmation for the specified transaction.";
            }

            return ByteBuffer();
        }

        bool VerifyPaymentProof(const ByteBuffer& data)
        {
            PaymentInfo pi = PaymentInfo::FromByteBuffer(data);
            
            if (!pi.IsValid())
            {
                return false;
            }

            LOG_INFO() << "Payment tx details:\n" << pi.to_string() << "Verified.";

            return true;
        }

        std::string ExportTxHistoryToCsv(const IWalletDB& db)
        {
            std::stringstream ss;
            ss << "Type" << ","
               << "Date | Time" << ","
               << "\"Amount, BEAM\"" << ","
               << "Status" << ","
               << "Sending address" << ","
               << "Receiving address" << ","
               << "\"Transaction fee, BEAM\"" << ","
               << "Transaction ID" << ","
               << "Kernel ID" << "," 
               << "Comment" << std::endl;

            for (const auto& tx : db.getTxHistory())
            {
                ss << (tx.m_sender ? "Send BEAM" : "Receive BEAM") << ","
                   << format_timestamp(kTimeStampFormatCsv, tx.m_createTime * 1000, false) << ","
                   << "\"" << PrintableAmount(tx.m_amount, true) << "\"" << ","
                   << tx.getStatusString() << ","
                   << std::to_string(tx.m_sender ? tx.m_myId : tx.m_peerId) << ","
                   << std::to_string(!tx.m_sender ? tx.m_myId : tx.m_peerId) << ","
                   << "\"" << PrintableAmount(tx.m_fee, true) << "\"" << ","
                   << to_hex(tx.m_txId.data(), tx.m_txId.size()) << ","
                   << std::to_string(tx.m_kernelID) << ","
                   << std::string { tx.m_message.begin(), tx.m_message.end() } << std::endl;
            }
            return ss.str();
        }

        namespace
        {
            void LogSqliteError(void* pArg, int iErrCode, const char* zMsg)
            {
                LOG_ERROR() << "(" << iErrCode << ") " << zMsg;
            }
        }

        void HookErrors()
        {
            sqlite3_config(SQLITE_CONFIG_LOG, LogSqliteError, nullptr);
        }
    }

    ////////////////////////
    // WalletAddress
    WalletAddress::WalletAddress()
        : m_walletID(Zero)
        , m_createTime(0)
        , m_duration(AddressExpiration24h)
        , m_OwnID(false)
    {}

    bool WalletAddress::operator == (const WalletAddress& other) const
    {
        return m_walletID == other.m_walletID && m_OwnID == other.m_OwnID;
    }

    bool WalletAddress::operator != (const WalletAddress& other) const
    {
        return !(*this == other);
    }

    bool WalletAddress::isExpired() const
    {
        return getTimestamp() > getExpirationTime();
    }

    Timestamp WalletAddress::getCreateTime() const
    {
        return m_createTime;
    }

    Timestamp WalletAddress::getExpirationTime() const
    {
        if (m_duration == AddressExpirationNever)
        {
            return Timestamp(-1);
        }
        return m_createTime + m_duration;
    }

    void WalletAddress::setLabel(const std::string& label)
    {
        m_label = label;
    }

    void WalletAddress::setExpiration(WalletAddress::ExpirationStatus status)
    {
        switch (status)
        {
        case ExpirationStatus::Expired:
            {
                assert(m_createTime < getTimestamp() - 1);
                m_duration = getTimestamp() - m_createTime - 1;
                break;
            }
        case ExpirationStatus::OneDay:
            {
                // set expiration date since current timestamp
                auto delta = getTimestamp() - m_createTime;
                m_duration = delta + WalletAddress::AddressExpiration24h;
                break;
            }
        case ExpirationStatus::Never:
            {
                m_duration = AddressExpirationNever;
                break;
            }
        
        default:
            break;
        }
    }
}
