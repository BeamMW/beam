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

#include "utility/logger.h"
#include "utility/helpers.h"
#include "sqlite/sqlite3.h"
#include "core/block_rw.h"
#include "wallet/core/common.h"
#include "wallet/core/common_utils.h"
#include <sstream>
#include <boost/functional/hash.hpp>
#ifndef __EMSCRIPTEN__
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <emscripten.h>
#include <emscripten/threading.h>
#include <filesystem>
namespace fs = std::filesystem;
#endif
#include <core/block_crypt.h>
#include <core/shielded.h>
#include "nlohmann/json.hpp"
#include "utility/std_extension.h"
#include "keykeeper/local_private_key_keeper.h"
#include "strings_resources.h"
#include "core/uintBig.h"
#include "utility/test_helpers.h"
#include <queue>
#include <unordered_map>
#include <boost/algorithm/string.hpp>

#include "base_transaction.h"
#include "node/processor.h"
#include "wallet/core/strings_resources.h"
#include <boost/format.hpp>

#define NOSEP
#define COMMA ", "
#define AND " AND "


#define ENUM_STORAGE_ID(each, sep, obj) \
    each(Type,           ID.m_Type,     INTEGER NOT NULL, obj) sep \
    each(SubKey,         ID.m_SubIdx,   INTEGER NOT NULL, obj) sep \
    each(Number,         ID.m_Idx,      INTEGER NOT NULL, obj) sep \
    each(amount,         ID.m_Value,    INTEGER NOT NULL, obj) sep \
    each(assetId,        ID.m_AssetID,  INTEGER, obj)

#define ENUM_STORAGE_FIELDS(each, sep, obj) \
    each(maturity,       maturity,            INTEGER NOT NULL, obj) sep \
    each(confirmHeight,  confirmHeight,       INTEGER, obj) sep \
    each(spentHeight,    spentHeight,         INTEGER, obj) sep \
    each(createTxId,     createTxId,          BLOB, obj) sep \
    each(spentTxId,      spentTxId,           BLOB, obj) sep \
    each(sessionId,      OBSOLETTEsessionId,  INTEGER NOT NULL, obj)

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
#define LASER_CHANNELS_NAME "LaserChannels"
#define LASER_ADDRESSES_NAME "LaserAddresses"
#define LASER_OPEN_STATES_NAME "LaserOpenStates"
#define LASER_UPDATES_NAME "LaserUpdates"
#define ASSETS_NAME "Assets"
#define SHIELDED_COINS_NAME "ShieldedCoins"
#define NOTIFICATIONS_NAME "notifications"
#define EXCHANGE_RATES_NAME "exchangeRates"
#define EXCHANGE_RATES_HISTORY_NAME "exchangeRatesHistory"
#define VOUCHERS_NAME "vouchers"
#define COIN_CONFIRMATIONS_COUNT "confirmations_count"
#define EVENTS_NAME "events"
#define TX_SUMMARY_NAME "tx_summary"

#define ENUM_VARIABLES_FIELDS(each, sep, obj) \
    each(name,  name,  TEXT UNIQUE, obj) sep \
    each(value, value, BLOB, obj)

#define VARIABLES_FIELDS ENUM_VARIABLES_FIELDS(LIST, COMMA, )

#define ENUM_ADDRESS_FIELDS(each, sep, obj) \
    each(walletID,       walletID,       BLOB, obj) sep \
    each(label,          label,          TEXT NOT NULL, obj) sep \
    each(category,       category,       TEXT, obj) sep \
    each(createTime,     createTime,     INTEGER, obj) sep \
    each(duration,       duration,       INTEGER, obj) sep \
    each(OwnID,          OwnID,          INTEGER NOT NULL, obj) sep \
    each(Identity,       Identity,       BLOB, obj) sep \
    each(Address,        Address,        TEXT NOT NULL PRIMARY KEY, obj) 

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

#define ENUM_LASER_CHANNEL_FIELDS(each, sep, obj) \
    each(chID,             chID,             BLOB NOT NULL PRIMARY KEY, obj) sep \
    each(myWID,            myWID,            BLOB NOT NULL, obj) sep \
    each(trgWID,           trgWID,           BLOB NOT NULL, obj) sep \
    each(state,            state,            INTEGER NOT NULL, obj) sep \
    each(fee,              fee,              INTEGER, obj) sep \
    each(locktime,         locktime,         INTEGER, obj) sep \
    each(amountMy,         amountMy,         INTEGER, obj) sep \
    each(amountTrg,        amountTrg,        INTEGER, obj) sep \
    each(amountCurrentMy,  amountCurrentMy,  INTEGER, obj) sep \
    each(amountCurrentTrg, amountCurrentTrg, INTEGER, obj) sep \
    each(lockHeight,       lockHeight,       INTEGER, obj) sep \
    each(bbsTimestamp,     bbsTimestamp,     INTEGER, obj) sep \
    each(data,             data,             BLOB, obj)

#define LASER_CHANNEL_FIELDS ENUM_LASER_CHANNEL_FIELDS(LIST, COMMA, )

#define ENUM_ASSET_FIELDS(each, sep, obj) \
    each(ID,             ID,            INTEGER NOT NULL PRIMARY KEY,  obj) sep \
    each(Value,          Value,         BLOB,              obj) sep \
    each(Owner,          Owner,         BLOB NOT NULL,     obj) sep \
    each(LockHeight,     LockHeight,    INTEGER,           obj) sep \
    each(Metadata,       Metadata,      BLOB,              obj) sep \
    each(RefreshHeight,  RefreshHeight, INTEGER NOT NULL,  obj) sep \
    each(IsOwned,        IsOwned,       INTEGER,           obj)

#define ASSET_FIELDS ENUM_ASSET_FIELDS(LIST, COMMA, )

#define ENUM_SHIELDED_COIN_FIELDS(each, sep, obj) \
    each(Key,                   CoinID.m_Key,         BLOB NOT NULL PRIMARY KEY, obj) sep \
    each(User,                  CoinID.m_User,        BLOB, obj) sep \
    each(ID,                    TxoID,                INTEGER NOT NULL, obj) sep \
    each(assetID,               CoinID.m_AssetID,     INTEGER, obj) sep \
    each(value,                 CoinID.m_Value,       INTEGER NOT NULL, obj) sep \
    each(confirmHeight,         confirmHeight,        INTEGER, obj) sep \
    each(spentHeight,           spentHeight,          INTEGER, obj) sep \
    each(createTxId,            createTxId,           BLOB, obj) sep \
    each(spentTxId,             spentTxId,            BLOB, obj)

#define SHIELDED_COIN_FIELDS ENUM_SHIELDED_COIN_FIELDS(LIST, COMMA, )

#define ENUM_NOTIFICATION_FIELDS(each, sep, obj) \
    each(ID,            ID,             BLOB NOT NULL PRIMARY KEY, obj) sep \
    each(type,          type,           INTEGER,            obj) sep \
    each(state,         state,          INTEGER,            obj) sep \
    each(createTime,    createTime,     INTEGER,            obj) sep \
    each(content,       content,        BLOB NOT NULL,      obj)

#define NOTIFICATION_FIELDS ENUM_NOTIFICATION_FIELDS(LIST, COMMA, )

#define ENUM_EXCHANGE_RATES_FIELDS(each, sep, obj) \
    each(cfrom,         from.m_value,   TEXT NOT NULL,      obj) sep \
    each(cto,           to.m_value,     TEXT NOT NULL,      obj) sep \
    each(rate,          rate,           INTEGER,            obj) sep \
    each(updateTime,    updateTime,     INTEGER,            obj)

#define EXCHANGE_RATES_FIELDS ENUM_EXCHANGE_RATES_FIELDS(LIST, COMMA, )

#define ENUM_EXCHANGE_RATES_HISTORY_FIELDS(each, sep, obj) \
    each(height,        height,         INTEGER,            obj) sep \
    each(cfrom,         from.m_value,   TEXT NOT NULL,      obj) sep \
    each(cto,           to.m_value,     TEXT NOT NULL,      obj) sep \
    each(rate,          rate,           INTEGER,            obj) sep \
    each(updateTime,    updateTime,     INTEGER,            obj)

#define EXCHANGE_RATES_HISTORY_FIELDS ENUM_EXCHANGE_RATES_HISTORY_FIELDS(LIST, COMMA, )

#define ENUM_VOUCHERS_FIELDS(each, sep, obj) \
    each(WalletID,  WalletID,  BLOB NOT NULL, obj) sep \
    each(Voucher,   Voucher,   BLOB NOT NULL UNIQUE, obj) sep \
    each(Flags,     Flags,     INTEGER, obj)

#define VOUCHERS_FIELDS ENUM_VOUCHERS_FIELDS(LIST, COMMA, )


#define ENUM_EVENTS_FIELDS(each, sep, obj) \
    each(Height,  Height,  INTEGER NOT NULL, obj) sep \
    each(Body,    Body,   BLOB NOT NULL, obj) sep \
    each(Key,     Key,    BLOB NOT NULL, obj)

#define EVENTS_FIELDS ENUM_EVENTS_FIELDS(LIST, COMMA, )

#define ENUM_TX_SUMMARY_FIELDS(each) \
    each(CreateTime, Timestamp) \
    BEAM_TX_LIST_FILTER_MAP(each)

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
        struct VoucherFlags
        {
            static constexpr uint16_t Default  = 0;
            static constexpr uint16_t Preserve = 1;
            static constexpr uint16_t Deleted  = 2;
        };

        void throwIfError(int res, sqlite3* db)
        {
            if (res == SQLITE_OK)
            {
                return;
            }
            stringstream ss;
            ss << "sqlite error code=" << res << ", " << sqlite3_errmsg(db);
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

        struct CoinSelector
        {
            typedef std::vector<Coin> Coins;
            typedef std::vector<size_t> Indexes;

            using Result = pair<Amount, Indexes>;

            const Coins& m_Coins; // input coins must be in ascending order, without zeroes
            
            CoinSelector(const Coins& coins)
                :m_Coins(coins)
            {
            }

            static const uint32_t s_Factor = 16;
            static const uint64_t s_NoOverflowSrc = uint64_t(-1) / s_Factor;

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

                uint32_t get_Slot_Fast(Amount v) const
                {
                    uint64_t val = uint64_t(v) * s_Factor;
                    assert(val / s_Factor == v); // overflow should not happen
                    val /= m_Goal;

                    return (val < s_Factor) ?
                        static_cast<uint32_t>(val) :
                        s_Factor;
                }

                uint32_t get_Slot_Big(Amount v) const
                {
                    // Use slower 'robust' arithmetics, guaranteed not to overflow
                    uintBigFor<uint32_t>::Type val;
                    val.SetDiv(uintBigFrom(v) * uintBigFrom(s_Factor), uintBigFrom(m_Goal));

                    uint32_t res;
                    val.Export(res);

                    std::setmin(res, s_Factor);
                    return res;
                }

                uint32_t get_Slot(Amount v) const
                {
                    if (v <= s_NoOverflowSrc)
                    {
                        uint32_t res = get_Slot_Fast(v);
                        //assert(get_Slot_Big(v) == res);
                        return res;
                    }

                    return get_Slot_Big(v);
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
            fromByteBuffer(blob, value);
        }

        vector<Coin> converIDsToCoins(const vector<Coin::ID>& coinIDs)
        {
            vector<Coin> coins(coinIDs.size());
            for (size_t i = 0; i < coins.size(); ++i)
            {
                coins[i].m_ID = coinIDs[i];
            }
            return coins;
        }

        Height DeduceTxProofHeightImpl(const IWalletDB& walletDB, const TxID& txID, TxType type)
        {
            Height height = 0;

            if (type == TxType::AssetInfo)
            {
                storage::getTxParameter(walletDB, txID, TxParameterID::AssetConfirmedHeight, height);
            }
            else
            {
                storage::getTxParameter(walletDB, txID, TxParameterID::KernelProofHeight, height);
            }

            return height;
        }
    }

    namespace sqlite
    {
        struct Statement
        {
            Statement(const WalletDB* db, const char* sql, bool privateDB = false)
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
            
            template<typename EnumType, typename = EnumTypesOnly<EnumType>>
            void bind(int col, EnumType type)
            {
                bind(col, underlying_cast(type));
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

            void bind(int col, const Blob& b)
            {
                // According to our convention empty blob is NOT NULL, it should be an empty BLOB field.
                // During initialization from buffer, if the buffer size is 0 - the x.p is left uninitialized.
                //
                // In sqlite code if x.p is NULL - it would treat the field as NULL, rather than an empty blob.
                // And if the uninitialized x.p is occasionally NULL - we get wrong behavior.
                //
                // Hence - we work this around, use `this`, as an arbitrary non-NULL pointer
                const void* pPtr = b.n ? b.p : this;
                bind(col, pPtr, b.n);
            }

            template<uint32_t nBytes_>
            void bind(int col, const uintBig_t<nBytes_>& data)
            {
                bind(col, data.m_pData, static_cast<size_t>(nBytes_));
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

            void bind(int col, const ShieldedTxo::BaseKey& x)
            {
                const auto& b = _buffers.emplace_back(toByteBuffer(x));
                bind(col, b);
            }

            void bind(int col, const ShieldedTxo::Voucher& x)
            {
                const auto& b = _buffers.emplace_back(toByteBuffer(x));
                bind(col, b);
            }

            void bind(int col, const ShieldedTxo::User& x)
            {
                bind(col, &x, sizeof(x));
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

            void bind(int col, const ECC::Scalar& scalar)
            {
                const auto& b = _buffers.emplace_back(toByteBuffer(scalar));
                bind(col, b);
            }

            void bind(int col, const ECC::Point& point)
            {
                const auto& b = _buffers.emplace_back(toByteBuffer(point));
                bind(col, b);
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

            template<typename EnumType, typename = EnumTypesOnly<EnumType>>
            void get(int col, EnumType& enumValue)
            {
                UnderlyingType<EnumType> temp;
                get(col, temp);
                enumValue = static_cast<EnumType>(temp);
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

            void get(int col, ShieldedTxo::BaseKey& x)
            {
                ByteBuffer b;
                get(col, b);
                fromByteBuffer(b, x);
            }

            void get(int col, ShieldedTxo::Voucher& x)
            {
                ByteBuffer b;
                get(col, b);
                fromByteBuffer(b, x);
            }

            void get(int col, ShieldedTxo::User& x)
            {
                // read/write as a blob, skip serialization
                getBlobStrict(col, &x, sizeof(x));
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

            void get(int col, PeerID& x)
            {
                if (!getBlobSafe(col, &x, sizeof(x)))
                {
                    x = Zero;
                }
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

            void get(int col, ECC::Scalar& scalar)
            {
                ByteBuffer b;
                get(col, b);
                fromByteBuffer(b, scalar);
            }

            void get(int col, ECC::Point& point)
            {
                ByteBuffer b;
                get(col, b);
                fromByteBuffer(b, point);
            }

            template<uint32_t nBytes_>
            void get(int col, uintBig_t<nBytes_>& data)
            {
                uint32_t size = sqlite3_column_bytes(_stm, col);
                if (size == nBytes_)
                    memcpy(data.m_pData, sqlite3_column_blob(_stm, col), size);
                else
                    throw std::runtime_error("wrong blob size");
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

            bool IsNull(int col)
            {
                return SQLITE_NULL == sqlite3_column_type(_stm, col);
            }

            ~Statement()
            {
                sqlite3_finalize(_stm);
            }
        private:
            WalletDB* _walletDB;
            sqlite3 * _db;
            sqlite3_stmt* _stm;
            std::vector<ByteBuffer> _buffers;
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
                int ret = sqlite3_exec(_db, "BEGIN EXCLUSIVE;", nullptr, nullptr, nullptr);
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
        const char* kStateSummaryShieldedOutsDBPath = "StateSummaryShieldedOuts";
        const char* kMaxPrivacyLockTimeLimitHours = "MaxPrivacyLockTimeLimitHours";
        constexpr char kIsTreasuryHandled[] = "IsTreasuryHandled";
        constexpr char kNeedToRequestBodies[] = "NeedToRequestBodies";
        constexpr char s_szNextEvt[] = "NextUtxoEvent"; // any event, not just UTXO. The name is for historical reasons
        const uint8_t kDefaultMaxPrivacyLockTimeLimitHours = 72;
        const int BusyTimeoutMs = 5000;
        const int DbVersion   =30;
        const int DbVersion29 = 29;
        const int DbVersion28 = 28;
        const int DbVersion27 = 27;
        const int DbVersion26 = 26;
        const int DbVersion25 = 25;
        const int DbVersion24 = 24;
        const int DbVersion23 = 23;
        const int DbVersion22 = 22;
        const int DbVersion21 = 21;
        const int DbVersion20 = 20;
        const int DbVersion19 = 19;
        const int DbVersion18 = 18;
        const int DbVersion17 = 17;
        const int DbVersion16 = 16;
        const int DbVersion15 = 15;
        const int DbVersion14 = 14;
        const int DbVersion13 = 13;
        const int DbVersion12 = 12;
        const int DbVersion11 = 11;
        const int DbVersion10 = 10;
    }

    Coin::Coin(Amount amount /* = 0 */, Key::Type keyType /* = Key::Type::Regular */, Asset::ID assetId /* = 0 */)
        : m_status{ Status::Unavailable }
        , m_maturity{ MaxHeight }
        , m_confirmHeight{ MaxHeight }
        , m_spentHeight{ MaxHeight }
        , m_OBSOLETTEsessionId(EmptyCoinSession)
    {
        m_ID = Zero;
        m_ID.m_Value = amount;
        m_ID.m_Type = keyType;
        m_ID.m_AssetID = assetId;
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

    bool Coin::isChange() const
    {
        return m_ID.m_Type == Key::Type::Change;
    }

    bool Coin::isAsset() const
    {
        return m_ID.m_AssetID != 0;
    }

    bool Coin::isAsset(Asset::ID assetId) const
    {
        return isAsset() && (m_ID.m_AssetID == assetId);
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

    Height Coin::get_Maturity(Height offset/* = 0*/) const
    {
        return IsMaturityValid() ? m_maturity + offset : MaxHeight;
    }

    bool Coin::operator==(const Coin& other) const
    {
        return other.m_ID == m_ID;
    }

    bool Coin::operator!=(const Coin& other) const
    {
        return !(other == *this);
    }

#pragma pack (push, 1)
    struct CoinIDPacked
    {
        // for historical reasons - make AssetID 1st member to keep bkwd compatibility
        // during serialization/deserialization leading zeroes are trimmed
        uintBigFor<Asset::ID>::Type m_AssetID;
        Key::ID::Packed m_Kid;
        uintBigFor<Amount>::Type m_Value;
    };
#pragma pack (pop)

    std::string toString(const CoinID& id)
    {
        CoinIDPacked packed;
        packed.m_Kid = id;
        packed.m_Value = id.m_Value;

        if (!id.m_AssetID)
            return to_hex(&packed.m_Kid, sizeof(packed) - sizeof(packed.m_AssetID));

        packed.m_AssetID = id.m_AssetID;
        return to_hex(&packed, sizeof(packed));
    }

    string Coin::toStringID() const
    {
        return toString(m_ID);
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
            {Consumed,      "consumed"},
        };

        return Strings[m_status];
    }

    boost::optional<Coin::ID> Coin::FromString(const std::string& str)
    {
        bool isValid = false;
        auto byteBuffer = from_hex(str, &isValid);
        if (isValid && byteBuffer.size() <= sizeof(CoinIDPacked))
        {
            CoinIDPacked packed;
            ZeroObject(packed);
            uint8_t* p = reinterpret_cast<uint8_t*>(&packed) + sizeof(CoinIDPacked) - byteBuffer.size();
            copy_n(byteBuffer.begin(), byteBuffer.size(), p);
            Coin::ID id;
            Cast::Down<Key::ID>(id) = packed.m_Kid;
            packed.m_Value.Export(id.m_Value);
            packed.m_AssetID.Export(id.m_AssetID);
            return id;
        }
        return boost::optional<Coin::ID>();
    }

    bool WalletDB::isInitialized(const string& path)
    {
#ifdef WIN32
        return fs::exists(Utf8toUtf16(path.c_str()));
#else
        return fs::exists(path);
#endif
    }

    namespace
    {
        bool IsTableCreated(const WalletDB* db, const char* tableName)
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
    
        void CreateAddressesTable(sqlite3* db, const std::string& tableName)
        {
            const std::string req = "CREATE TABLE " + tableName + " (" ENUM_ADDRESS_FIELDS(LIST_WITH_TYPES, COMMA, ) ") WITHOUT ROWID;"
                              "CREATE INDEX " + tableName + "WalletIDIndex ON " + tableName + "(WalletID);";
            int ret = sqlite3_exec(db, req.c_str(), nullptr, nullptr, nullptr);
            throwIfError(ret, db);
        }

        void CreateVariablesTable(sqlite3* db)
        {
            const char* req = "CREATE TABLE " VARIABLES_NAME " (" ENUM_VARIABLES_FIELDS(LIST_WITH_TYPES, COMMA, ) ");";
            int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, db);
        }

        void CreateTxParamsIndex(sqlite3* db)
        {
            const char* req = "CREATE INDEX IF NOT EXISTS TxParamsIndex ON " TX_PARAMS_NAME " (txID,subTxID,paramID);";
            int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, db);
        }

        void CreateTxParamsTable(sqlite3* db)
        {
            const char* req = "CREATE TABLE " TX_PARAMS_NAME " (" ENUM_TX_PARAMS_FIELDS(LIST_WITH_TYPES, COMMA, ) ", PRIMARY KEY (txID, subTxID, paramID)) WITHOUT ROWID;";
            int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, db);
            CreateTxParamsIndex(db);
        }

        void CreateStatesTable(sqlite3* db)
        {
            const char* req = "CREATE TABLE [" TblStates "] ("
                "[" TblStates_Height    "] INTEGER NOT NULL PRIMARY KEY,"
                "[" TblStates_Hdr        "] BLOB NOT NULL)";
            int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, db);
        }

        void CreateLaserTables(sqlite3* db)
        {
            const char* req = "CREATE TABLE " LASER_CHANNELS_NAME " (" ENUM_LASER_CHANNEL_FIELDS(LIST_WITH_TYPES, COMMA, ) ") WITHOUT ROWID;";
            int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, db);

            CreateAddressesTable(db, LASER_ADDRESSES_NAME);

            LOG_INFO() << "Create laser tables";
        }

        void CreateAssetsTable(sqlite3* db)
        {
            assert(db != nullptr);
            const char* req = "CREATE TABLE " ASSETS_NAME " (" ENUM_ASSET_FIELDS(LIST_WITH_TYPES, COMMA, ) ") WITHOUT ROWID;"
                              "CREATE UNIQUE INDEX OwnerIndex ON " ASSETS_NAME "(Owner);"
                              "CREATE INDEX RefreshHeightIndex ON " ASSETS_NAME "(RefreshHeight);";
            const auto ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, db);
        }

        void CreateEventsTable(sqlite3* db)
        {
            assert(db != nullptr);
            const char* req = "CREATE TABLE " EVENTS_NAME " (" ENUM_EVENTS_FIELDS(LIST_WITH_TYPES, COMMA, ) ");"
                "CREATE INDEX EventsIndex ON " EVENTS_NAME "(Height, Body);"
                "CREATE INDEX EventsKeyIndex ON " EVENTS_NAME "(Key);";
            const auto ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, db);
        }

        void CreateTxSummaryTable(sqlite3* db)
        {
            assert(db != nullptr);
#define MACRO(id, type) "," #id " INTEGER" 
            const char* req = "CREATE TABLE " TX_SUMMARY_NAME " (TxID BLOB NOT NULL PRIMARY KEY " ENUM_TX_SUMMARY_FIELDS(MACRO) ") WITHOUT ROWID;"
#undef MACRO

#define STR(s) #s 
#define MACRO(id, type) "CREATE INDEX " STR(id##Index) " ON " TX_SUMMARY_NAME "(" #id ");"
            ENUM_TX_SUMMARY_FIELDS(MACRO)
#undef MACRO
#undef STR
                ;
            const auto ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, db);
        }

        void MigrateAssetsFrom20(sqlite3* db)
        {
            assert(db != nullptr);

            // assets table changed: value from INTEGER to BLOB
            // move old data to temp table
            {
                const char* req = "ALTER TABLE " ASSETS_NAME " RENAME TO " ASSETS_NAME "_del;"
                                  "DROP INDEX OwnerIndex;"
                                  "DROP INDEX RefreshHeightIndex;";
                int ret = sqlite3_exec(db, req, NULL, NULL, NULL);
                throwIfError(ret, db);
            }

            // create new table
            CreateAssetsTable(db);

            // migration
            {
                const char* req = "INSERT INTO " ASSETS_NAME " (" ENUM_ASSET_FIELDS(LIST, COMMA, ) ") SELECT " ENUM_ASSET_FIELDS(LIST, COMMA, ) " FROM " ASSETS_NAME "_del;";
                int ret = sqlite3_exec(db, req, NULL, NULL, NULL);
                throwIfError(ret, db);
            }

            // remove tmp table
            {
                const char* req = "DROP TABLE " ASSETS_NAME "_del;";
                int ret = sqlite3_exec(db, req, NULL, NULL, NULL);
                throwIfError(ret, db);
            }
        }

        void CreateNotificationsTable(sqlite3* db)
        {
            const char* req = "CREATE TABLE " NOTIFICATIONS_NAME " (" ENUM_NOTIFICATION_FIELDS(LIST_WITH_TYPES, COMMA, ) ") WITHOUT ROWID;";
            int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, db);
        }

        void CreateExchangeRatesTable(sqlite3* db)
        {
            const char* req = "CREATE TABLE " EXCHANGE_RATES_NAME " (" ENUM_EXCHANGE_RATES_FIELDS(LIST_WITH_TYPES, COMMA, ) ", PRIMARY KEY (cfrom, cto)) WITHOUT ROWID;";
            int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, db);
        }

        void CreateExchangeRatesHistoryTable(sqlite3* db)
        {
            const char* req = "CREATE TABLE " EXCHANGE_RATES_HISTORY_NAME " (" ENUM_EXCHANGE_RATES_HISTORY_FIELDS(LIST_WITH_TYPES, COMMA, ) ", PRIMARY KEY (cfrom, cto, updateTime)) WITHOUT ROWID;";
            int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, db);
        }

        void AddAddressIdentityColumn(const WalletDB* walletDB, sqlite3* db)
        {
            const char* req = "ALTER TABLE " ADDRESSES_NAME " ADD Identity BLOB NULL;";
            int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, db);

            const char* req_laser_addr_identity_exist =
                "SELECT COUNT(*) AS CNTREC FROM pragma_table_info('" LASER_ADDRESSES_NAME "') WHERE name='Identity';";
            int laser_addr_identity_exist = 0;
            for (sqlite::Statement stm(walletDB, req_laser_addr_identity_exist); stm.step();)
            {
                stm.get(0, laser_addr_identity_exist);
            }

            if (!laser_addr_identity_exist)
            {
                const char* req_laser = "ALTER TABLE " LASER_ADDRESSES_NAME " ADD Identity BLOB NULL;";
                ret = sqlite3_exec(db, req_laser, nullptr, nullptr, nullptr);
                throwIfError(ret, db);
            }
        }

        void CreateShieldedCoinsTable(sqlite3* db)
        {
            const char* req = "CREATE TABLE " SHIELDED_COINS_NAME " (" ENUM_SHIELDED_COIN_FIELDS(LIST_WITH_TYPES, COMMA, ) ") WITHOUT ROWID;";
            int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, db);
        }

        void CreateShieldedCoinsTableIndex(sqlite3* db)
        {
            const char* req = "CREATE INDEX " SHIELDED_COINS_NAME "SpendIdx ON " SHIELDED_COINS_NAME "(spentHeight);";
            int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, db);
        }

        void CreateVouchersTable(sqlite3* db)
        {
            const char* req = "CREATE TABLE " VOUCHERS_NAME " (" ENUM_VOUCHERS_FIELDS(LIST_WITH_TYPES, COMMA, ) ");"
                "CREATE INDEX WalletIDIndex ON " VOUCHERS_NAME "(WalletID);";
            int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, db);
        }

        bool VouchersHasFlags(const WalletDB* db)
        {
            sqlite::Statement stm(db, "SELECT* FROM PRAGMA_table_info('Vouchers') WHERE name = 'Flags';");
            return stm.step();
        }

        void AddVouchersFlagsColumn(sqlite3* db)
        {
            const char* req = "ALTER TABLE " VOUCHERS_NAME " ADD Flags INTEGER NULL;";
            int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
            throwIfError(ret, db);
        }

        void MigrateAddressesFrom24(WalletDB* walletDB, sqlite3* db, const std::string& tableName)
        {
            // move old data to temp table
            if (!IsTableCreated(walletDB, (tableName + "_del").c_str()))
            {
                const std::string req = "ALTER TABLE " + tableName + " RENAME TO " + tableName + "_del;";
                int ret = sqlite3_exec(db, req.c_str(), NULL, NULL, NULL);
                throwIfError(ret, db);
            }

            // create new table
            CreateAddressesTable(db, tableName);

            bool isLaser = tableName == LASER_ADDRESSES_NAME;
            // migration
            {
                sqlite::Statement stm((const WalletDB*)walletDB, ("SELECT * FROM " + tableName + "_del;").c_str());

                while (stm.step())
                {
                    WalletAddress addr;
                    stm.get(0, addr.m_walletID);
                    stm.get(1, addr.m_label);
                    stm.get(2, addr.m_category);
                    stm.get(3, addr.m_createTime);
                    stm.get(4, addr.m_duration);
                    stm.get(5, addr.m_OwnID);
                    stm.get(6, addr.m_Identity);

                    if (addr.m_walletID != Zero)
                    {
                        addr.m_Address = std::to_string(addr.m_walletID);
                        walletDB->saveAddress(addr, isLaser);
                    }
                }
            }

            // remove tmp table
            {
                const std::string req = "DROP TABLE " + tableName + "_del;";
                int ret = sqlite3_exec(db, req.c_str(), NULL, NULL, NULL);
                throwIfError(ret, db);
            }
        }

        void MigrateAddressesFrom24(WalletDB* walletDB, sqlite3* db)
        {
            MigrateAddressesFrom24(walletDB, db, ADDRESSES_NAME);
            MigrateAddressesFrom24(walletDB, db, LASER_ADDRESSES_NAME);
        }

        void MigrateTransactionsFrom25(WalletDB* walletDB)
        {
            sqlite::Statement stm((const WalletDB*)walletDB, "SELECT txID, value FROM " TX_PARAMS_NAME " WHERE subTxID=?1 AND paramID=?2;");
            stm.bind(1, kDefaultSubTxID);
            stm.bind(2, TxParameterID::OriginalToken);
            while (stm.step())
            {
                TxID txID;
                std::string originalAddress;
                ByteBuffer buf;
                stm.get(0, txID);
                stm.get(1, buf);
                if (fromByteBuffer(buf, originalAddress))
                {
                    auto addressType = GetAddressType(originalAddress);
                    storage::setTxParameter(*walletDB, txID, TxParameterID::AddressType, addressType, false);
                }
            }
        }

        struct ExchangeRate29
        {
            enum class CurrType: uint32_t
            {};

            CurrType    m_from = CurrType(0);
            CurrType    m_to   = CurrType(0);
            Amount      m_rate = 0;
            Timestamp   m_updateTime = 0;
            SERIALIZE(m_from, m_to, m_rate, m_updateTime);
        };


        Currency exchangeCurr29to30(ExchangeRate29::CurrType curr29)
        {
            switch(static_cast<uint32_t>(curr29)) {
                case 0: return  Currency::BEAM();
                case 1: return  Currency::BTC();
                case 2: return  Currency::LTC();
                case 3: return  Currency::QTUM();
                case 4: return  Currency::USD();
                case 5: return  Currency::DOGE();
                case 6: return  Currency::DASH();
                case 7: return  Currency::ETH();
                case 8: return  Currency::DAI();
                case 9: return  Currency::USDT();
                case 10: return Currency::WBTC();
                case 11: return Currency::BCH();
                default: return Currency::UNKNOWN();
            }
        }

        void MigrateRatesFrom29 (WalletDB* walletDB, sqlite3* db)
        {
            if (!IsTableCreated(walletDB, EXCHANGE_RATES_NAME "_del"))
            {
                const char* req = "ALTER TABLE " EXCHANGE_RATES_NAME " RENAME TO " EXCHANGE_RATES_NAME "_del;";
                int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
                throwIfError(ret, db);
            }

            if (!IsTableCreated(walletDB, EXCHANGE_RATES_NAME))
            {
                CreateExchangeRatesTable(db);
            }

            {
                const char* req = "SELECT currency, unit, rate, updateTime FROM " EXCHANGE_RATES_NAME "_del;";
                for (sqlite::Statement stm(walletDB, req); stm.step();)
                {
                    ExchangeRate rate;

                    ExchangeRate29::CurrType from29;
                    ExchangeRate29::CurrType to29;

                    stm.get(0, from29);
                    stm.get(1, to29);
                    stm.get(2, rate.m_rate);
                    stm.get(3, rate.m_updateTime);

                    rate.m_from = exchangeCurr29to30(from29);
                    rate.m_to   = exchangeCurr29to30(to29);

                    walletDB->saveExchangeRate(rate);
                }
            }

            {
                const char* req = "DROP TABLE " EXCHANGE_RATES_NAME "_del;";
                int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
                throwIfError(ret, db);
            }
        }

        void MigrateRatesHistoryFrom29 (WalletDB* walletDB, sqlite3* db)
        {
            if (!IsTableCreated(walletDB, EXCHANGE_RATES_HISTORY_NAME "_del"))
            {
                const char* req = "ALTER TABLE " EXCHANGE_RATES_HISTORY_NAME " RENAME TO " EXCHANGE_RATES_HISTORY_NAME "_del;";
                int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
                throwIfError(ret, db);
            }

            if (!IsTableCreated(walletDB, EXCHANGE_RATES_HISTORY_NAME))
            {
                CreateExchangeRatesHistoryTable(db);
            }

            {
                const char* req = "SELECT currency, unit, rate, updateTime, height FROM " EXCHANGE_RATES_HISTORY_NAME "_del;";
                for (sqlite::Statement stm(walletDB, req); stm.step();)
                {
                    ExchangeRateAtPoint rate;

                    ExchangeRate29::CurrType from29;
                    ExchangeRate29::CurrType to29;

                    stm.get(0, from29);
                    stm.get(1, to29);
                    stm.get(2, rate.m_rate);
                    stm.get(3, rate.m_updateTime);
                    stm.get(4, rate.m_height);

                    rate.m_from = exchangeCurr29to30(from29);
                    rate.m_to   = exchangeCurr29to30(to29);

                    walletDB->saveExchangeRate(rate);
                }
            }

            {
                const char* req = "DROP TABLE " EXCHANGE_RATES_HISTORY_NAME "_del;";
                int ret = sqlite3_exec(db, req, nullptr, nullptr, nullptr);
                throwIfError(ret, db);
            }
        }

        void MigrateTxRatesFrom29to30(WalletDB* walletDB, sqlite3* db)
        {
            struct UpdateInfo
            {
                TxID txID = TxID();
                uint64_t subTxID = kDefaultSubTxID;
                std::vector<ExchangeRate> rates;
            };

            std::vector<UpdateInfo> updates;

            sqlite::Statement stm((const WalletDB*)walletDB, "SELECT txID, subTxID, value FROM " TX_PARAMS_NAME " WHERE paramID=?1;");
            stm.bind(1, TxParameterID::ExchangeRates);
            while (stm.step())
            {
                UpdateInfo updateInfo;
                ByteBuffer buffer;

                stm.get(0, updateInfo.txID);
                stm.get(1, updateInfo.subTxID);
                stm.get(2, buffer);

                std::vector<ExchangeRate29> rates29;
                if (fromByteBuffer(buffer, rates29))
                {
                    for (const auto& rate29: rates29)
                    {
                        ExchangeRate rate30;
                        rate30.m_from       = exchangeCurr29to30(rate29.m_from);
                        rate30.m_to         = exchangeCurr29to30(rate29.m_to);
                        rate30.m_rate       = rate29.m_rate;
                        rate30.m_updateTime = rate29.m_updateTime;
                        updateInfo.rates.push_back(rate30);
                    }
                    updates.push_back(updateInfo);
                }
                else
                {
                    throw std::runtime_error("MigrateTxRatesFrom29to30 - failed to read v29 rates vector");
                }
            }

            for (const auto& updateInfo: updates)
            {
                storage::setTxParameter(*walletDB, updateInfo.txID, static_cast<SubTxID>(updateInfo.subTxID), TxParameterID::ExchangeRates, updateInfo.rates, false);
                LOG_INFO() << "Rates convert for " << updateInfo.txID << ", rcnt: " << updateInfo.rates.size();
            }
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
        CreateAddressesTable(db, ADDRESSES_NAME);
        CreateTxParamsTable(db);
        CreateStatesTable(db);
        CreateLaserTables(db);
        CreateAssetsTable(db);
        CreateShieldedCoinsTable(db);
        CreateShieldedCoinsTableIndex(db);
        CreateNotificationsTable(db);
        CreateExchangeRatesTable(db);
        CreateVouchersTable(db);
        CreateExchangeRatesHistoryTable(db);
        CreateEventsTable(db);
        CreateTxSummaryTable(db);
    }

    std::shared_ptr<WalletDB> WalletDB::initBase(const string& path, const SecString& password, bool separateDBForPrivateData)
    {
        if (isInitialized(path))
        {
            LOG_ERROR() << path << " already exists.";
            throw DatabaseException("Database already exists");
        }

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
        auto walletDB = make_shared<WalletDB>(db, sdb);
        walletDB->onPrepareToModify();
        createTables(walletDB->_db, walletDB->m_PrivateDB);

        storage::setVar(*walletDB, Version, DbVersion);

        return walletDB;

    }

    void WalletDB::storeOwnerKey()
    {
        ECC::NoLeak<ECC::HKdfPub::Packed> packedOwnerKey;
        assert(m_pKdfOwner->ExportP(nullptr) == sizeof(packedOwnerKey));
        m_pKdfOwner->ExportP(&packedOwnerKey);

        storage::setVar(*this, OwnerKey, packedOwnerKey.V);
    }

    struct WalletDB::LocalKeyKeeper
        :public LocalPrivateKeyKeeperStd
    {
        using LocalPrivateKeyKeeperStd::LocalPrivateKeyKeeperStd;

        struct UsedSlots
        {
            static const char s_szDbName[];

            State::UsedMap m_Used;

            template <typename Archive>
            void serialize(Archive& ar)
            {
                ar & m_Used;
            }

            void Load(WalletDB& db)
            {
                ByteBuffer buf;
                if (db.getBlob(s_szDbName, buf))
                {
                    Deserializer der;
                    der.reset(buf);
                    der & *this;
                }
            }

            void Save(WalletDB& db)
            {
                Serializer ser;
                ser & *this;

                db.setVarRaw(s_szDbName, ser.buffer().first, ser.buffer().second);
            }
        };

        class UsedSlotsWrk
            :public UsedSlots
        {
            LocalKeyKeeper* m_pLocal;

            void SwapSafe()
            {
                if (m_pLocal)
                    m_pLocal->m_State.m_Used.swap(m_Used);
            }

        public:

            UsedSlotsWrk(LocalKeyKeeper* pLocal)
                :m_pLocal(pLocal)
            {
                SwapSafe();
            }

            ~UsedSlotsWrk()
            {
                SwapSafe();
            }
        };

    };

    const char WalletDB::LocalKeyKeeper::UsedSlots::s_szDbName[] = "KeyKeeperSlots";

    void WalletDB::FromMaster(const ECC::uintBig& seed)
    {
        ECC::HKdf::Create(m_pKdfMaster, seed);
        FromMaster();
    }

    void WalletDB::FromMaster()
    {
        assert(m_pKdfMaster);
        m_pKdfOwner = m_pKdfMaster;
        m_pKdfSbbs = m_pKdfMaster;

        if (!m_pKeyKeeper)
        {
            m_pKeyKeeper = std::make_shared<LocalKeyKeeper>(m_pKdfMaster);
            m_pLocalKeyKeeper = &Cast::Up<LocalKeyKeeper>(*m_pKeyKeeper);
        }

        UpdateLocalSlots();
    }

    void WalletDB::FromKeyKeeper()
    {
        assert(!m_pKdfMaster);

        if (!m_pKeyKeeper)
            throw std::runtime_error("Key keeper required");

        IPrivateKeyKeeper2::Method::get_Kdf m;
        m.m_Root = true;
        m_pKeyKeeper->InvokeSync(m);

        if (m.m_pKdf)
        {
            // trusted mode.
            m_pKdfMaster = std::move(m.m_pKdf);
            FromMaster();
            return;
        }

        if (!m.m_pPKdf)
            throw std::runtime_error("can't get owner key");

        m_pKdfOwner = std::move(m.m_pPKdf);

        // trustless mode. create SBBS Kdf from a child PKdf. It won't be directly accessible from the owner key
        m.m_Root = false;
        m.m_iChild = Key::Index(-1); // definitely won't collude with a coin child Kdf (for coins high byte is reserved for scheme)

        m_pKeyKeeper->InvokeSync(m);

        if (!m.m_pPKdf)
            throw std::runtime_error("can't get sbbs key");

        ECC::Scalar::Native sk;
        m.m_pPKdf->DerivePKey(sk, Zero);

        ECC::NoLeak<ECC::Scalar> s;
        s.V = sk;

        ECC::HKdf::Create(m_pKdfSbbs, s.V.m_Value);

        UpdateLocalSlots();

    }

    void WalletDB::UpdateLocalSlots()
    {
        assert(m_pKeyKeeper);

        IPrivateKeyKeeper2::Method::get_NumSlots m = { 0 };
        m.m_Count = 0;
        if ((IPrivateKeyKeeper2::Status::Success != m_pKeyKeeper->InvokeSync(m)) || !m.m_Count)
            throw std::runtime_error("key keeper no slots");

        m_KeyKeeperSlots = m.m_Count;

        LocalKeyKeeper::UsedSlots us;
        bool bKeep = false;

        try
        {
            us.Load(*this);
            if (us.m_Used.empty() || (us.m_Used.rbegin()->first < m.m_Count))
                bKeep = true;
        }
        catch (...)
        {
        }

        if (!bKeep)
        {
            us.m_Used.clear();
            us.Save(*this);
        }

        if (m_pLocalKeyKeeper)
        {
            ECC::GenRandom(m_pLocalKeyKeeper->m_State.m_hvLast);

            if (bKeep)
                // restore used slots
                m_pLocalKeyKeeper->m_State.m_Used.swap(us.m_Used);
        }
    }

    IWalletDB::Ptr WalletDB::initNoKeepr(const std::string& path, const SecString& password, bool separateDBForPrivateData)
    {
        return initBase(path, password, separateDBForPrivateData);
    }

    IWalletDB::Ptr WalletDB::init(const string& path, const SecString& password, const ECC::NoLeak<ECC::uintBig>& secretKey, bool separateDBForPrivateData)
    {
        std::shared_ptr<WalletDB> walletDB = initBase(path, password, separateDBForPrivateData);
        if (walletDB)
        {
            walletDB->FromMaster(secretKey.V);

            walletDB->setPrivateVarRaw(WalletSeed, &secretKey.V, sizeof(secretKey.V)); // store master key
            walletDB->storeOwnerKey(); // store owner key (public)

            walletDB->flushDB();
            walletDB->m_Initialized = true;
        }

        return walletDB;
    }

    IWalletDB::Ptr WalletDB::init(const string& path, const SecString& password, const IPrivateKeyKeeper2::Ptr& pKeyKeeper, bool separateDBForPrivateData)
    {
        std::shared_ptr<WalletDB> walletDB = initBase(path, password, separateDBForPrivateData);
        if (walletDB)
        {
            walletDB->m_pKeyKeeper = pKeyKeeper;
            walletDB->FromKeyKeeper();

            walletDB->storeOwnerKey(); // store owner key (public)

            walletDB->flushDB();
            walletDB->m_Initialized = true;
        }

        return walletDB;
    }

    IWalletDB::Ptr WalletDB::open(const string& path, const SecString& password)
    {
        return open(path, password, nullptr);
    }

    IWalletDB::Ptr WalletDB::open(const string& path, const SecString& password, const IPrivateKeyKeeper2::Ptr& pKeyKeeper)
    {
        if (!isInitialized(path))
        {
            LOG_ERROR() << path << " not found, please init the wallet before.";
            throw DatabaseNotFoundException();
        }

        sqlite3* db = nullptr;
        OpenAndMigrateIfNeeded(path, &db, password);
        sqlite3* sdb = db;
        string privatePath = path + ".private";
        bool separateDBForPrivateData = isInitialized(privatePath);
        if (separateDBForPrivateData)
        {
            OpenAndMigrateIfNeeded(privatePath, &sdb, password);
        }

        auto walletDB = make_shared<WalletDB>(db, sdb);
        {
            int ret = sqlite3_busy_timeout(walletDB->_db, BusyTimeoutMs);
            throwIfError(ret, walletDB->_db);
        }
        {
            int version = 0;
            storage::getVar(*walletDB, Version, version);

            // start new transaction
            walletDB->onPrepareToModify();
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

                        for (sqlite::Statement stm(walletDB.get(), req); stm.step(); )
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
                            stm.get(11, coin.m_OBSOLETTEsessionId);

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
                    LOG_INFO() << "Converting DB from format 14...";

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
                // no break;

                case DbVersion15:
                    LOG_INFO() << "Converting DB from format 15...";

                    // originally there was a coin migration, because "assetId" column was added.
                    // We now postpone this migration till 18-19, where the "assetId" column was moved into an index.

                    // no break; 
                case DbVersion16:
                    LOG_INFO() << "Converting DB from format 16...";
                    CreateLaserTables(walletDB->_db);
                    // no break;
                case DbVersion17:
                    LOG_INFO() << "Converting DB from format 17...";
                    CreateAssetsTable(walletDB->_db);
                    // no break

                case DbVersion18:
                    LOG_INFO() << "Converting DB from format 18...";
                    walletDB->MigrateCoins();
                    CreateNotificationsTable(walletDB->_db);
                    CreateExchangeRatesTable(walletDB->_db);
                    AddAddressIdentityColumn(walletDB.get(), walletDB->_db);
                    // no break

                case DbVersion19:
                    LOG_INFO() << "Converting DB from format 19...";
                    CreateShieldedCoinsTable(walletDB->_db);
                    // no break

                case DbVersion20:
                    LOG_INFO() << "Converting DB from format 20...";
                    MigrateAssetsFrom20(walletDB->_db);
                    // no break

                case DbVersion21:
                    LOG_INFO() << "Converting DB from format 21...";
                    CreateVouchersTable(db);
                    // no break

                case DbVersion22:
                    CreateShieldedCoinsTableIndex(db);
                    // no break

                case DbVersion23:
                    LOG_INFO() << "Converting DB from format 23...";
                    if (!VouchersHasFlags(walletDB.get()))
                    {
                        AddVouchersFlagsColumn(db);
                    }
                    // no break

                case DbVersion24:
                    LOG_INFO() << "Converting DB from format 24...";
                    MigrateAddressesFrom24(walletDB.get(), walletDB->_db);
                    // no break

                case DbVersion25:
                    LOG_INFO() << "Converting DB from format 25...";
                    CreateExchangeRatesHistoryTable(walletDB->_db);
                    // no break

                case DbVersion26:
                    LOG_INFO() << "Converting DB from format 26...";
                    MigrateTransactionsFrom25(walletDB.get());
                    // no break

                case DbVersion27:
                    LOG_INFO() << "Converting DB from format 27...";
                    CreateTxSummaryTable(walletDB->_db);
                    walletDB->FillTxSummaryTable();
                    // no break

                case DbVersion28:
                    LOG_INFO() << "Converting DB from format 28...";
                    CreateEventsTable(walletDB->_db);
                    // no break

                case DbVersion29:
                    LOG_INFO() << "Converting DB from format 29...";
                    MigrateRatesFrom29(walletDB.get(), walletDB->_db);
                    MigrateRatesHistoryFrom29(walletDB.get(), walletDB->_db);
                    MigrateTxRatesFrom29to30(walletDB.get(), walletDB->_db);

                    // no break
                    storage::setVar(*walletDB, Version, DbVersion);

                case DbVersion:
                    CreateTxParamsIndex(walletDB->_db);
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
            catch (std::runtime_error& err)
            {
                LOG_ERROR() << "Database migration failed: " << err.what();
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
                walletDB->FromMaster(seed.V);
            }
            else
            {
                ECC::NoLeak<ECC::HKdfPub::Packed> packedOwnerKey;
                bool bHadOwnerKey = storage::getVar(*walletDB, OwnerKey, packedOwnerKey.V);

                if (pKeyKeeper || !bHadOwnerKey)
                {
                    walletDB->m_pKeyKeeper = pKeyKeeper;
                    walletDB->FromKeyKeeper();

                    if (bHadOwnerKey)
                    {
                        // consistency check. Make sure there's an agreement w.r.t. stored owner key
                        ECC::NoLeak<ECC::HKdfPub::Packed> keyCurrent;
                        walletDB->m_pKdfOwner->ExportP(&keyCurrent);

                        if (memcmp(&packedOwnerKey, &keyCurrent, sizeof(keyCurrent)))
                            throw std::runtime_error("Key keeper is different");
                    }
                    else
                        walletDB->storeOwnerKey();
                }
                else
                {
                    assert(bHadOwnerKey);
                    // Read-only wallet.
                    walletDB->m_pKdfOwner = std::make_shared<ECC::HKdfPub>();
                    Cast::Up<ECC::HKdfPub>(*walletDB->m_pKdfOwner).Import(packedOwnerKey.V);
                }

            }
        }
        walletDB->getVarRaw(COIN_CONFIRMATIONS_COUNT, &walletDB->m_coinConfirmationsOffset, sizeof(uint32_t));
        walletDB->m_Initialized = true;
        return static_pointer_cast<IWalletDB>(walletDB);
    }

    void WalletDB::MigrateCoins()
    {
        // migrate coins
        if (!IsTableCreated(this, STORAGE_NAME "_del"))
        {
            const char* req =
                "ALTER TABLE " STORAGE_NAME " RENAME TO " STORAGE_NAME "_del;"
                "DROP INDEX CoinIndex;"
                "DROP INDEX ConfirmIndex;";

            int ret = sqlite3_exec(_db, req, NULL, NULL, NULL);
            throwIfError(ret, _db);
        }

        if (!IsTableCreated(this, STORAGE_NAME))
        {
            CreateStorageTable(_db);
        }

        {
            const char* req = "SELECT * FROM " STORAGE_NAME "_del;";
            for (sqlite::Statement stm(this, req); stm.step();)
            {
                Coin coin;
                stm.get(0, coin.m_ID.m_Type);
                stm.get(1, coin.m_ID.m_SubIdx);
                stm.get(2, coin.m_ID.m_Idx);
                stm.get(3, coin.m_ID.m_Value);
                stm.get(4, coin.m_maturity);
                stm.get(5, coin.m_confirmHeight);
                stm.get(6, coin.m_spentHeight);
                stm.get(7, coin.m_createTxId);
                stm.get(8, coin.m_spentTxId);
                stm.get(9, coin.m_OBSOLETTEsessionId);

                saveCoin(coin);
            }
        }

        {
            const char* req = "DROP TABLE " STORAGE_NAME "_del;";
            int ret = sqlite3_exec(_db, req, NULL, NULL, NULL);
            throwIfError(ret, _db);
        }
    }

    WalletDB::WalletDB(sqlite3* db)
        : WalletDB(db, db)
    {

    }

    WalletDB::WalletDB(sqlite3* db, sqlite3* sdb)
        : _db(db)
        , m_PrivateDB(sdb)
        , m_IsFlushPending(false)
        , m_mandatoryTxParams{
            TxParameterID::TransactionType,
            TxParameterID::CreateTime}
    {

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
        return m_pKdfMaster;
    }

    Key::IPKdf::Ptr WalletDB::get_OwnerKdf() const
    {
        return m_pKdfOwner;
    }

    Key::IKdf::Ptr WalletDB::get_SbbsKdf() const
    {
        return m_pKdfSbbs;
    }

    IPrivateKeyKeeper2::Ptr WalletDB::get_KeyKeeper() const
    {
        return m_pKeyKeeper;
    }

    IPrivateKeyKeeper2::Slot::Type WalletDB::SlotAllocate()
    {
        IPrivateKeyKeeper2::Slot::Type iSlot = IPrivateKeyKeeper2::Slot::Invalid;

        if (m_pKeyKeeper)
        {
            LocalKeyKeeper::UsedSlotsWrk us(m_pLocalKeyKeeper);
            if (!m_pLocalKeyKeeper)
                us.Load(*this);

            assert(us.m_Used.size() <= m_KeyKeeperSlots);
            if (us.m_Used.size() < m_KeyKeeperSlots)
            {
                if (us.m_Used.empty())
                    iSlot = 0;
                else
                {
                    iSlot = us.m_Used.rbegin()->first + 1;
                    if (iSlot >= m_KeyKeeperSlots)
                    {
                        // find free from the beginning
                        iSlot = 0;
                        for (LocalKeyKeeper::State::UsedMap::iterator it = us.m_Used.begin(); it->first == iSlot; ++it)
                            iSlot++;
                    }
                }

                ECC::Hash::Value& hv = us.m_Used[iSlot];
                if (m_pLocalKeyKeeper)
                    m_pLocalKeyKeeper->m_State.Regenerate(hv);
                else
                    hv = Zero;
            }

            us.Save(*this);
        }

        return iSlot;
    }

    void WalletDB::SlotFree(IPrivateKeyKeeper2::Slot::Type iSlot)
    {
        if (m_pKeyKeeper && (IPrivateKeyKeeper2::Slot::Invalid != iSlot))
        {
            LocalKeyKeeper::UsedSlotsWrk us(m_pLocalKeyKeeper);
            if (!m_pLocalKeyKeeper)
                us.Load(*this);

            LocalKeyKeeper::State::UsedMap::iterator it = us.m_Used.find(iSlot);
            if (us.m_Used.end() != it)
            {
                us.m_Used.erase(it);
                us.Save(*this);
            }
        }
    }

    bool IWalletDB::get_CommitmentSafe(ECC::Point& comm, const CoinID& cid, IPrivateKeyKeeper2* pKeyKeeper)
    {
        ECC::Point::Native comm2;
        if (!pKeyKeeper || (IPrivateKeyKeeper2::Status::Success != pKeyKeeper->get_Commitment(comm2, cid)))
        {
            Key::Index idx;
            if (cid.get_ChildKdfIndex(idx))
                return false; // child kdf is required

            Key::IPKdf::Ptr pOwner = get_OwnerKdf();
            assert(pOwner); // must always be available

            CoinID::Worker(cid).Recover(comm2, *pOwner);
        }

        comm = comm2;
        return true;
    }

    bool IWalletDB::get_CommitmentSafe(ECC::Point& comm, const CoinID& cid)
    {
        return get_CommitmentSafe(comm, cid, get_KeyKeeper().get());
    }

    bool IWalletDB::IsRecoveredMatch(CoinID& cid, const ECC::Point& comm)
    {
        IPrivateKeyKeeper2::Ptr pKeyKeeper = get_KeyKeeper();
        
        ECC::Point comm2;
        if (!get_CommitmentSafe(comm2, cid, pKeyKeeper.get()))
        {
            assert(!pKeyKeeper); // read-only mode
            return true; // Currently we skip this 2nd-stage validation in read-only wallet for UTXOs generated by the child kdf (such as miner key).
        }

        if (comm2 == comm)
            return true;

        if (!cid.IsBb21Possible())
            return false;

        cid.set_WorkaroundBb21();

        get_CommitmentSafe(comm2, cid, pKeyKeeper.get());
        return (comm2 == comm);
    }

	void IWalletDB::ImportRecovery(const std::string& path, INegotiatorGateway& gateway)
	{
		IRecoveryProgress prog;
		BEAM_VERIFY(ImportRecovery(path, gateway, prog));
	}

	bool IWalletDB::ImportRecovery(const std::string& path, INegotiatorGateway& gateway, IRecoveryProgress& prog)
	{
        struct MyParser
            :public RecoveryInfo::IRecognizer
        {
            IWalletDB& m_This;
            INegotiatorGateway& m_Gateway;
            IRecoveryProgress& m_Progr;
            TxoID m_ShieldedOuts = 0;

            MyParser(IWalletDB& db, INegotiatorGateway& gateway, IRecoveryProgress& progr)
                :m_This(db)
                ,m_Gateway(gateway)
                ,m_Progr(progr)
            {
            }

            virtual bool OnProgress(uint64_t nPos, uint64_t nTotal) override
            {
                return m_Progr.OnProgress(nPos, nTotal);
            }

            virtual bool OnStates(std::vector<Block::SystemState::Full>& vec) override
            {
                if (!vec.empty())
                    m_This.get_History().AddStates(&vec.front(), vec.size());

                return true;
            }

            virtual bool OnUtxoRecognized(Height h, const Output& outp, CoinID& cid, const Output::User& user) override
            {
                if (m_This.IsRecoveredMatch(cid, outp.m_Commitment))
                {
                    Coin c;
                    c.m_ID = cid;
                    m_This.findCoin(c); // in case it exists already - fill its parameters

                    c.m_maturity = outp.get_MinMaturity(h);
                    c.m_confirmHeight = h;

                    LOG_INFO() << "CoinID: " << c.m_ID << " Maturity=" << c.m_maturity << " Recovered";

                    m_This.saveCoin(c);

                    // create event to allow mobile node to detect this UTXO spending
                    proto::Event::Utxo evt;
                    evt.m_Flags = proto::Event::Flags::Add;
                    evt.m_Cid = cid;
                    evt.m_Commitment = outp.m_Commitment;
                    evt.m_Maturity = c.m_maturity;
                    evt.m_User = user;

                    Serializer ser;
                    ser& uintBigFrom(NodeProcessor::EventKey::s_IdxOutput);
                    ser& evt.s_Type;
                    ser& evt;

                    const NodeProcessor::EventKey::Utxo& key = outp.m_Commitment;
                    m_This.insertEvent(h, Blob(ser.buffer().first, static_cast<uint32_t>(ser.buffer().second)), Blob(&key, sizeof(NodeProcessor::EventKey::Utxo)));
                }

                return true;
            }

            typedef std::map<ECC::Point, ShieldedTxo::BaseKey> ShieldedSpendKeyMap;
            ShieldedSpendKeyMap m_mapShielded;

            virtual bool OnAssetRecognized(Asset::Full&) override
            {
                // TODO
                return true;
            }

            virtual bool OnShieldedOutRecognized(const ShieldedTxo::DescriptionOutp& dout, const ShieldedTxo::DataParams& pars, Key::Index nIdx) override
            {
                ShieldedCoin sc;
                pars.ToID(sc.m_CoinID);
                sc.m_CoinID.m_Key.m_nIdx = nIdx;
                sc.m_TxoID = dout.m_ID;

                sc.m_confirmHeight = dout.m_Height;
                sc.m_spentHeight = MaxHeight;

                m_This.saveShieldedCoin(sc);

                LOG_INFO() << "Shielded output, ID: " << dout.m_ID << " Confirmed, Height=" << dout.m_Height;

                m_mapShielded[pars.m_Ticket.m_SpendPk] = sc.m_CoinID.m_Key;

                storage::restoreTransactionFromShieldedCoin(m_This, sc, m_Gateway);

                return true;
            }

            virtual bool OnShieldedIn(const ShieldedTxo::DescriptionInp& dinp) override
            {
                ShieldedSpendKeyMap::iterator it = m_mapShielded.find(dinp.m_SpendPk);
                if (m_mapShielded.end() != it)
                {
                    auto shieldedCoin = m_This.getShieldedCoin(it->second);
                    if (shieldedCoin)
                    {
                        shieldedCoin->m_spentHeight = dinp.m_Height;
                        m_This.saveShieldedCoin(*shieldedCoin);

                        LOG_INFO() << "Shielded input, TxoID: " << shieldedCoin->m_TxoID << " Spent, Height=" << dinp.m_Height;
                    }

                    m_mapShielded.erase(it);
                }

                return true;
            }

            bool OnShieldedOut(const ShieldedTxo::DescriptionOutp& d, const ShieldedTxo& s, const ECC::Hash::Value& hvMsg, Height h) override
            {
                m_ShieldedOuts++;
                return RecoveryInfo::IRecognizer::OnShieldedOut(d, s, hvMsg, h);
            }


        };

        MyParser p(*this, gateway, prog);
        p.Init(get_OwnerKdf());

        if (p.Proceed(path.c_str()))
        {
            storage::setTreasuryHandled(*this, true);
            set_ShieldedOuts(p.m_ShieldedOuts);
            Block::SystemState::Full sTip;
            get_History().get_Tip(sTip);
            storage::setNextEventHeight(*this, sTip.m_Height + 1); // next
            storage::setNeedToRequestBodies(*this, true); // temporarilly enable body requests, to solve lag in blocks
            return true;
        }
        return false;
	}

    void IWalletDB::get_SbbsPeerID(ECC::Scalar::Native& sk, PeerID& pid, uint64_t ownID)
    {
        Key::IKdf::Ptr pKdfSbbs = get_SbbsKdf();
        if (!pKdfSbbs)
            throw CannotGenerateSecretException();

        ECC::Hash::Value hv;
        Key::ID(ownID, Key::Type::Bbs).get_Hash(hv);

        pKdfSbbs->DeriveKey(sk, hv);
        pid.FromSk(sk);
    }

    void IWalletDB::get_SbbsWalletID(ECC::Scalar::Native& sk, WalletID& wid, uint64_t ownID)
    {
        get_SbbsPeerID(sk, wid.m_Pk, ownID);
        wid.SetChannelFromPk();
    }

    void IWalletDB::get_SbbsWalletID(WalletID& wid, uint64_t ownID)
    {
        ECC::Scalar::Native sk;
        get_SbbsWalletID(sk, wid, ownID);
    }

    bool IWalletDB::ValidateSbbsWalletID(const WalletID& wid, uint64_t ownID)
    {
        WalletID wid2;
        get_SbbsWalletID(wid2, ownID);
        return wid == wid2;
    }

    void IWalletDB::createAddress(WalletAddress& addr)
    {
        addr.m_createTime = beam::getTimestamp();
        addr.m_OwnID = AllocateKidRange(1);

        get_SbbsWalletID(addr.m_walletID, addr.m_OwnID);
        get_Identity(addr.m_Identity, addr.m_OwnID);

        LOG_INFO() << boost::format(kWalletIdNewGenerated) % std::to_string(addr.m_walletID);

        if (!addr.m_label.empty())
        {
            LOG_INFO() << boost::format(kAddrNewGeneratedLabel) % addr.m_label;
        }

        addr.m_Address = std::to_string(addr.m_walletID);
    }

    void IWalletDB::get_Identity(PeerID& pid, uint64_t ownID) const
    {
        ECC::Hash::Value hv;
        Key::ID(ownID, Key::Type::WalletID).get_Hash(hv);
        ECC::Point::Native pt;
        get_OwnerKdf()->DerivePKeyG(pt, hv);
        pid = ECC::Point(pt).m_X;
    }

    TxoID IWalletDB::get_ShieldedOuts() const
    {
        TxoID ret = 0;
        storage::getVar(*this, kStateSummaryShieldedOutsDBPath, ret);
        return ret;
    }

    void IWalletDB::set_ShieldedOuts(TxoID val)
    {
        storage::setVar(*this, kStateSummaryShieldedOutsDBPath, val);
    }

    uint8_t IWalletDB::get_MaxPrivacyLockTimeLimitHours() const
    {
        uint8_t ret = kDefaultMaxPrivacyLockTimeLimitHours;
        storage::getVar(*this, kMaxPrivacyLockTimeLimitHours, ret);
        return ret;
    }

    void IWalletDB::set_MaxPrivacyLockTimeLimitHours(uint8_t val)
    {
        storage::setVar(*this, kMaxPrivacyLockTimeLimitHours, val);
    }

    void IWalletDB::addStatusInterpreterCreator(TxType txType, TxStatusInterpreter::Creator interpreterCreator)
    {
        m_statusInterpreterCreators[txType] = interpreterCreator;
    }

    TxStatusInterpreter::Ptr IWalletDB::getStatusInterpreter(const TxParameters& txParams) const
    {
        if (auto txType = txParams.GetParameter<TxType>(TxParameterID::TransactionType); txType)
        {
            auto it = m_statusInterpreterCreators.find(*txType);
            if (it != m_statusInterpreterCreators.end())
            {
                auto creator = it->second;
                return creator(txParams);
            }
        }

        return std::make_shared<TxStatusInterpreter>(txParams);
    }

    vector<Coin> WalletDB::selectCoins(Amount amount, Asset::ID assetId)
    {
        return selectCoinsEx(amount, assetId, false);
    }

    void WalletDB::selectCoins2(Height h, Amount nTrg, Asset::ID aid, std::vector<Coin>& vSelStd, std::vector<ShieldedCoin>& vSelShielded, uint32_t nMaxShielded, bool bCanReturnLess)
    {
        if (!nTrg)
            return;

        Amount nSel = 0;

        vector<ShieldedCoin::WithStatus> vShielded;
        size_t iPosShielded = 0;

        auto& fs = Transaction::FeeSettings::get(h);
        Amount feeShielded = fs.m_ShieldedInputTotal;

        if (nMaxShielded)
        {
            visitShieldedCoinsUnspent(
                [&vShielded, aid, feeShielded](const ShieldedCoin& coin) -> bool
                {
                    if ((ShieldedCoin::Status::Available == coin.m_Status) && (coin.m_CoinID.m_AssetID == aid))
                    {
                        // skip dust
                        bool bDust = !aid && (coin.m_CoinID.m_Value <= feeShielded);
                        if (!bDust)
                            vShielded.emplace_back().first = coin;
                    }
                    return true;
                }
            );

            if (!vShielded.empty())
            {
                TxoID nOuts = get_ShieldedOuts();
                for (auto& x : vShielded)
                    x.second.Init(x.first, nOuts);

                ShieldedCoin::Sort(vShielded);
            }

            for (; iPosShielded < vShielded.size(); iPosShielded++)
            {
                const auto& x = vShielded[iPosShielded];
                int n = x.second.get_SpendPriority();

                if (n < 0) // don't spend unless have to
                    break;

                if (!n && (nSel >= nTrg)) // don't spend if amount already reached
                    break;

                if (!aid)
                    nTrg += feeShielded;

                nSel += x.first.m_CoinID.m_Value;
                vSelShielded.push_back(x.first);

                if (vSelShielded.size() == nMaxShielded)
                    break;
            }
        }

        Amount nSelStd = selectCoinsStd(nTrg, nSel, aid, vSelStd);
        nSel += nSelStd;

        if (nSel >= nTrg)
            return; // ok

        for (; (nSel < nTrg) && (iPosShielded < vShielded.size()) && (vSelShielded.size() < nMaxShielded); iPosShielded++)
        {
            if (!aid)
                nTrg += feeShielded;

            const auto& x = vShielded[iPosShielded];

            nSel += x.first.m_CoinID.m_Value;
            vSelShielded.push_back(x.first);
        }

        if (nSel > nTrg)
        {
            // re-select the std coins. Maybe less would be neeed now
            nSel -= nSelStd;
            nSelStd = selectCoinsStd(nTrg, nSel, aid, vSelStd);
            nSel += nSelStd;
        }

        if ((nSel < nTrg) && !bCanReturnLess)
        {
            // failed to select needed amount. By convention don't return anything
            vSelStd.clear();
            vSelShielded.clear();
        }
    }

    Amount WalletDB::selectCoinsStd(Amount nTrg, Amount nSel, Asset::ID aid, std::vector<Coin>& vSelStd)
    {
        Amount nSelStd = 0;
        if (nSel < nTrg)
        {
            vSelStd = selectCoinsEx(nTrg - nSel, aid, true);

            for (size_t i = 0; i < vSelStd.size(); i++)
                nSelStd += vSelStd[i].m_ID.m_Value;
        }
        else
            vSelStd.clear();

        return nSelStd;
    }

    vector<Coin> WalletDB::selectCoinsEx(Amount amount, Asset::ID assetId, bool bCanReturnLess)
    {
        vector<Coin> coins, coinsSel;
        Block::SystemState::ID stateID = {};
        getSystemStateID(stateID);

        {
            const char* query = "SELECT " STORAGE_FIELDS " FROM " STORAGE_NAME " WHERE maturity>=0 AND maturity<=?1 AND spentHeight<0 ORDER BY amount ASC";

            sqlite::Statement stm(this, query);
            stm.bind(1, stateID.m_Height);

            while (stm.step())
            {
                auto& coin = coins.emplace_back();
                int colIdx = 0;
                ENUM_ALL_STORAGE_FIELDS(STM_GET_LIST, NOSEP, coin);

                storage::DeduceStatus(*this, coin, stateID.m_Height);
                if (Coin::Status::Available != coin.m_status)
                    coins.pop_back();
                else if (coin.m_ID.m_AssetID != assetId)
                    coins.pop_back();
                else
                {
                    if (coin.m_ID.m_Value >= amount)
                        break;
                }
            }
        }

        CoinSelector csel(coins);
        CoinSelector::Result res = csel.Select(amount);

        if (res.first >= amount)
        {
            coinsSel.reserve(res.second.size());

            for (size_t j = 0; j < res.second.size(); j++)
                coinsSel.push_back(std::move(coins[res.second[j]]));
        }
        else
        {
            if (bCanReturnLess)
                coinsSel.swap(coins);
        }

        return coinsSel;
    }

    std::vector<Coin> WalletDB::getNormalCoins(Asset::ID assetId) const
    {
        std::vector<Coin> coins;
        visitCoins([&coins, assetId](const Coin& coin) -> bool {
            if (coin.m_ID.m_AssetID == assetId)
            {
                coins.push_back(coin);
            }
            return true;
        });
        return coins;
    }

    std::vector<Coin> WalletDB::getAllNormalCoins() const
    {
        std::vector<Coin> coins;
        visitCoins([&coins](const Coin& coin) -> bool {
            coins.push_back(coin);
            return true;
        });
        return coins;
    }

    std::vector<Coin> WalletDB::getCoinsCreatedByTx(const TxID& txId) const
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

    std::vector<Coin> WalletDB::getCoinsByTx(const TxID& txId) const
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

    vector<Coin> WalletDB::getCoinsByID(const CoinIDList& ids) const
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

    bool WalletDB::saveCoinRaw(const Coin& coin)
    {
        if (!updateCoinRaw(coin))
        {
            insertCoinRaw(coin);
            return false; // inserted
        }
        return true; // updated
    }

    vector<Coin> WalletDB::getCoinsByRowIDs(const vector<int>& rowIDs) const
    {
        vector<Coin> coins;
        coins.reserve(rowIDs.size());
        Height h = getCurrentHeight();
        sqlite::Statement stm(this, "SELECT * FROM " STORAGE_NAME " WHERE rowid=?1");
        for (int rowid : rowIDs)
        {
            stm.bind(1, rowid);
            if (stm.step())
            {
                Coin& coin = coins.emplace_back();
                int colIdx = 0;
                ENUM_ALL_STORAGE_FIELDS(STM_GET_LIST, NOSEP, coin);

                storage::DeduceStatus(*this, coin, h);
            }
            stm.Reset();
        }
        return coins;
    }

    vector<Coin> WalletDB::getUpdatedCoins(const vector<Coin>& coins) const
    {
        vector<Coin> updatedCoins = coins;
        Height h = getCurrentHeight();
        for (Coin& coin : updatedCoins)
        {
            storage::DeduceStatus(*this, coin, h);
        }
        return updatedCoins;
    }

    Coin WalletDB::generateNewCoin(Amount amount, Asset::ID assetId)
    {
        Coin coin(amount, Key::Type::Regular, assetId);
        coin.m_ID.m_Idx = get_RandomID();

        // check for collisions
        Coin cDup;
        cDup.m_ID = coin.m_ID;
        while (findCoin(cDup))
        {
            cDup.m_ID.m_Idx++;
        }

        coin.m_ID.m_Idx = cDup.m_ID.m_Idx;
        return coin;
    }

    void WalletDB::storeCoin(Coin& coin)
    {
        coin.m_ID.m_Idx = get_RandomID();
        insertNewCoin(coin);
        notifyCoinsChanged(ChangeAction::Added, {coin});
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
        notifyCoinsChanged(ChangeAction::Added, coins);
    }

    void WalletDB::saveCoin(const Coin& coin)
    {
        bool updated = saveCoinRaw(coin);
        notifyCoinsChanged(updated ? ChangeAction::Updated : ChangeAction::Added, getUpdatedCoins({coin}));
    }

    void WalletDB::saveCoins(const vector<Coin>& coins)
    {
        if (coins.empty())
            return;

        for (auto& coin : coins)
        {
            saveCoinRaw(coin);
        }

        notifyCoinsChanged(ChangeAction::Updated, getUpdatedCoins(coins));
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

            notifyCoinsChanged(ChangeAction::Removed, converIDsToCoins(coins));
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

    void WalletDB::clearCoins()
    {
        sqlite::Statement stm(this, "DELETE FROM " STORAGE_NAME ";");
        stm.step();
        notifyCoinsChanged(ChangeAction::Reset, {});
    }

    void WalletDB::setCoinConfirmationsOffset(uint32_t offset)
    {
        setVarRaw(COIN_CONFIRMATIONS_COUNT, &offset, sizeof(uint32_t));
        m_coinConfirmationsOffset = offset;
    }

    uint32_t WalletDB::getCoinConfirmationsOffset() const
    {
        return m_coinConfirmationsOffset;
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

    struct WalletDB::ShieldedStatusCtx
    {
        Height m_hTip;

        ShieldedStatusCtx(const WalletDB& db)
        {
            m_hTip = db.getCurrentHeight();
        }
    };

    void WalletDB::visitShieldedCoins(std::function<bool(const ShieldedCoin& info)> func) const
    {
        ShieldedStatusCtx ssc(*this);

        sqlite::Statement stm(this, "SELECT " SHIELDED_COIN_FIELDS " FROM " SHIELDED_COINS_NAME " ORDER BY ID;");
        while (stm.step())
        {
            ShieldedCoin coin;

            int colIdx = 0;
            ENUM_SHIELDED_COIN_FIELDS(STM_GET_LIST, NOSEP, coin);

            storage::DeduceStatus(*this, coin, ssc.m_hTip);
            if (!func(coin))
                break;
        }
    }

    void WalletDB::visitShieldedCoinsUnspent(const std::function<bool(const ShieldedCoin& info)>& func) const
    {
        ShieldedStatusCtx ssc(*this);

        sqlite::Statement stm(this, "SELECT " SHIELDED_COIN_FIELDS " FROM " SHIELDED_COINS_NAME " WHERE spentHeight <0 ORDER BY ID;");
        while (stm.step())
        {
            ShieldedCoin coin;

            int colIdx = 0;
            ENUM_SHIELDED_COIN_FIELDS(STM_GET_LIST, NOSEP, coin);

            storage::DeduceStatus(*this, coin, ssc.m_hTip);
            if (!func(coin))
                break;
        }
    }

    void WalletDB::visitCoins(function<bool(const Coin& coin)> func) const
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
        vector<int> changedRows;
        {
            const char* req = "SELECT rowid FROM " STORAGE_NAME " WHERE confirmHeight > ?1 OR spentHeight > ?1;";
            sqlite::Statement stm(this, req);
            stm.bind(1, minHeight);
            while (stm.step())
            {
                int& rowid = changedRows.emplace_back();
                stm.get(0, rowid);
            }
        }
        if (!changedRows.empty())
        {
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
 
            notifyCoinsChanged(ChangeAction::Updated, getCoinsByRowIDs(changedRows));
        }
    }

    std::vector<ShieldedCoin> WalletDB::getAllShieldedCoins() const
    {
        std::vector<ShieldedCoin> coins;
        visitShieldedCoins([&coins] (const ShieldedCoin& coin) -> bool {
            coins.push_back(coin);
            return true;
        });
        return coins;
    }

    std::vector<ShieldedCoin> WalletDB::getShieldedCoins(Asset::ID assetId) const
    {
        std::vector<ShieldedCoin> coins;
        visitShieldedCoins([&coins, assetId] (const ShieldedCoin& coin) -> bool {
            if (coin.m_CoinID.m_AssetID == assetId)
            {
                coins.push_back(coin);
            }
            return true;
        });
        return coins;
    }

    boost::optional<ShieldedCoin> WalletDB::getShieldedCoin(const TxID& txId) const
    {
        ShieldedStatusCtx ssc(*this);

        sqlite::Statement stm(this, "SELECT " SHIELDED_COIN_FIELDS " FROM " SHIELDED_COINS_NAME " WHERE createTxId=?1 OR spentTxId=?1;");
        stm.bind(1, txId);

        if (stm.step())
        {
            ShieldedCoin coin;
            int colIdx = 0;
            ENUM_SHIELDED_COIN_FIELDS(STM_GET_LIST, NOSEP, coin);
            storage::DeduceStatus(*this, coin, ssc.m_hTip);
            return coin;
        }

        return {};
    }

    boost::optional<ShieldedCoin> WalletDB::getShieldedCoin(TxoID id) const
    {
        ShieldedStatusCtx ssc(*this);

        sqlite::Statement stm(this, "SELECT " SHIELDED_COIN_FIELDS " FROM " SHIELDED_COINS_NAME " WHERE ID = ?;");
        stm.bind(1, id);

        if (stm.step())
        {
            ShieldedCoin coin;
            int colIdx = 0;
            ENUM_SHIELDED_COIN_FIELDS(STM_GET_LIST, NOSEP, coin);
            storage::DeduceStatus(*this, coin, ssc.m_hTip);
            return coin;
        }

        return {};
    }

    boost::optional<ShieldedCoin> WalletDB::getShieldedCoin(const ShieldedTxo::BaseKey& key) const
    {
        ShieldedStatusCtx ssc(*this);

        sqlite::Statement stm(this, "SELECT " SHIELDED_COIN_FIELDS " FROM " SHIELDED_COINS_NAME " WHERE Key = ?;");
        stm.bind(1, key);

        if (stm.step())
        {
            ShieldedCoin coin;
            int colIdx = 0;
            ENUM_SHIELDED_COIN_FIELDS(STM_GET_LIST, NOSEP, coin);
            storage::DeduceStatus(*this, coin, ssc.m_hTip);
            return coin;
        }
        return {};
    }

    void WalletDB::clearShieldedCoins()
    {
        sqlite::Statement stm(this, "DELETE FROM " SHIELDED_COINS_NAME ";");
        stm.step();
        notifyShieldedCoinsChanged(ChangeAction::Reset, {});
    }

    void WalletDB::saveShieldedCoin(const ShieldedCoin& shieldedCoin)
    {
        saveShieldedCoinRaw(shieldedCoin);
    }

    void WalletDB::rollbackConfirmedShieldedUtxo(Height minHeight)
    {
        ShieldedStatusCtx ssc(*this);
        vector<ShieldedCoin> changedCoins;

        // 1. Undo 'spent' where necessary
        const char* req = "SELECT " ENUM_SHIELDED_COIN_FIELDS(LIST, COMMA, ) " FROM " SHIELDED_COINS_NAME " WHERE spentHeight > ?1;";
        sqlite::Statement stm(this, req);
        stm.bind(1, minHeight);

        while (stm.step())
        {
            ShieldedCoin& coin = changedCoins.emplace_back();
            int colIdx = 0;
            ENUM_SHIELDED_COIN_FIELDS(STM_GET_LIST, NOSEP, coin);

            coin.m_spentHeight = MaxHeight;
            updateShieldedCoinRaw(coin);

            storage::DeduceStatus(*this, coin, ssc.m_hTip);
            changedCoins.push_back(std::move(coin));
        }

        if (!changedCoins.empty())
        {
            notifyShieldedCoinsChanged(ChangeAction::Updated, changedCoins);
            changedCoins.clear();
        }

        // 2. For unspent - remove confirmHeight where applicable
        visitShieldedCoinsUnspent([&changedCoins, minHeight](const ShieldedCoin& coin) -> bool {
            if ((MaxHeight != coin.m_confirmHeight) && (coin.m_confirmHeight > minHeight))
                changedCoins.push_back(coin);
            return true;
            });

        if (!changedCoins.empty())
        {
            for (size_t i = 0; i < changedCoins.size(); i++)
            {
                ShieldedCoin& coin = changedCoins[i];
                coin.m_confirmHeight = MaxHeight;
                updateShieldedCoinRaw(coin);
                storage::DeduceStatus(*this, coin, ssc.m_hTip);
            }

            // TODO: erase coins completely with "Unavail" status
            notifyShieldedCoinsChanged(ChangeAction::Updated, changedCoins);
        }
    }

    void WalletDB::insertShieldedCoinRaw(const ShieldedCoin& coin)
    {
        const char* req = "INSERT INTO " SHIELDED_COINS_NAME " (" ENUM_SHIELDED_COIN_FIELDS(LIST, COMMA, ) ") VALUES(" ENUM_SHIELDED_COIN_FIELDS(BIND_LIST, COMMA, ) ");";
        sqlite::Statement stm(this, req);
        int colIdx = 0;
       
        ENUM_SHIELDED_COIN_FIELDS(STM_BIND_LIST, NOSEP, coin);
        stm.step();
    }

    bool WalletDB::updateShieldedCoinRaw(const ShieldedCoin& coin)
    {
        const char* req = "UPDATE " SHIELDED_COINS_NAME " SET " ENUM_SHIELDED_COIN_FIELDS(SET_LIST, COMMA, ) "WHERE Key = ?;";
        sqlite::Statement stm(this, req);
        int colIdx = 0;

        ENUM_SHIELDED_COIN_FIELDS(STM_BIND_LIST, NOSEP, coin);
        stm.bind(++colIdx, coin.m_CoinID.m_Key);
        stm.step();

        return sqlite3_changes(_db) > 0;
    }

    void WalletDB::saveShieldedCoinRaw(const ShieldedCoin& coin)
    {
        std::vector<ShieldedCoin> v = { coin };
        storage::DeduceStatus(*this, v[0], getCurrentHeight());
        if (!updateShieldedCoinRaw(coin))
        {
            insertShieldedCoinRaw(coin);
            notifyShieldedCoinsChanged(ChangeAction::Added, v);
            return;
        }
        notifyShieldedCoinsChanged(ChangeAction::Updated, v);
    }

    void WalletDB::visitTx(std::function<bool(const TxDescription&)> func, const TxListFilter& filter) const
    {
        helpers::StopWatch sw;
        sw.start();

        std::string query = "SELECT TxID FROM " TX_SUMMARY_NAME;
        std::vector<std::string> parts;
        std::string whereParams;

#define MACRO(id, type) \
        if (filter.m_##id) \
        { \
            std::string q(#id "="); \
            q.append(std::to_string((int)*filter.m_##id)); \
            parts.push_back(std::move(q)); \
        } 
        BEAM_TX_LIST_NORMAL_PARAM_MAP(MACRO)
        
        if (!parts.empty())
        {
            whereParams.append(boost::join(parts, " AND "));
            parts.clear();
        }

        BEAM_TX_LIST_HEIGHT_MAP(MACRO)
#undef MACRO
        if (!parts.empty())
        {
            if (whereParams.empty())
            {
                whereParams.append(" (");
            }
            else
            {
                whereParams.append(" AND (");
            }
            whereParams.append(boost::join(parts, " OR "))
                       .append(")");
        }

        if (!whereParams.empty())
        {
            query.append(" WHERE ");
            query.append(whereParams);
        }
        
        query.append(" ORDER BY CreateTime DESC");

        sqlite::Statement stm(this, query.c_str());
        sqlite::Statement stm2(this, "SELECT * FROM " TX_PARAMS_NAME " WHERE txID=?1;");
        TxID txID;
        while (stm.step())
        {
            stm.get(0, txID);

            auto t = getTxImpl(txID, stm2);
            if (t.is_initialized())
            {
                if (!func(*t))
                {
                    break;
                }
            }
        }

        sw.stop();
        LOG_DEBUG() << "visitTx  elapsed time: " << sw.milliseconds() << " ms\n";
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

            const char* gtParamReq = "SELECT * FROM " TX_PARAMS_NAME " WHERE txID=?1;";
            sqlite::Statement stm2(this, gtParamReq);

            while (stm.step())
            {
                TxID txID;
                stm.get(0, txID);
                auto t = getTxImpl(txID, stm2);
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
        return getTxImpl(txId, stm);
    }

    boost::optional<TxDescription> WalletDB::getTxImpl(const TxID& txId, sqlite::Statement& stm) const
    {
        stm.Reset();
        stm.bind(1, txId);

        TxDescription txDescription(txId);
        struct Set : public std::priority_queue<TxParameterID, std::vector<TxParameterID>, std::greater<TxParameterID> >
        {
            container_type::const_iterator begin() const noexcept { return c.begin(); }
            container_type::const_iterator end() const noexcept { return c.end(); }
        } gottenParams;

        while (stm.step())
        {
            TxParameter parameter = {};
            int colIdx = 0;
            ENUM_TX_PARAMS_FIELDS(STM_GET_LIST, NOSEP, parameter);
            auto parameterID = static_cast<TxParameterID>(parameter.m_paramID);

            txDescription.SetParameter(parameterID, std::move(parameter.m_value), static_cast<SubTxID>(parameter.m_subTxID));

            if (parameter.m_subTxID == kDefaultSubTxID)
            {
                gottenParams.emplace(parameterID);
            }
        }

        txDescription.fillFromTxParameters(txDescription);

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
        storage::setTxParameter(*this, p.m_txId, TxParameterID::AssetID, p.m_assetId, false);
        storage::setTxParameter(*this, p.m_txId, TxParameterID::AssetMetadata, p.m_assetMeta, false);
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
            // we left one record about tx type in order to avoid re-launching of deleted transaction
            const char* req = "DELETE FROM " TX_PARAMS_NAME " WHERE txID=?1 AND paramID!=?2;";
            sqlite::Statement stm(this, req);

            stm.bind(1, txId);
            stm.bind(2, TxParameterID::TransactionType);

            stm.step();
            deleteParametersFromCache(txId);
            notifyTransactionChanged(ChangeAction::Removed, { *tx });
        }

        {
            sqlite::Statement stm(this, "DELETE FROM " TX_SUMMARY_NAME " WHERE txID=?1;");
            stm.bind(1, txId);
            stm.step();
        }
    }

    void WalletDB::restoreCoinsSpentByTx(const TxID& txId)
    {
        std::vector<int> updatedRows;
        {
            const char* req = "SELECT rowid FROM " STORAGE_NAME " WHERE spentTxId=?1;";
            sqlite::Statement stm(this, req);
            stm.bind(1, txId);
            while(stm.step())
            {
                int& coin = updatedRows.emplace_back();
                stm.get(0, coin);
            }
        }
        if (!updatedRows.empty())
        {
            {
                const char* req = "UPDATE " STORAGE_NAME " SET spentTxId=NULL WHERE spentTxId=?1;";
                sqlite::Statement stm(this, req);
                stm.bind(1, txId);
                stm.step();
            }
            notifyCoinsChanged(ChangeAction::Updated, getCoinsByRowIDs(updatedRows));
        }
    }

    void WalletDB::deleteCoinsCreatedByTx(const TxID& txId)
    {
        std::vector<Coin> deletedItems;
        {
            const char* req = "SELECT " ENUM_ALL_STORAGE_FIELDS(LIST, COMMA, ) " FROM " STORAGE_NAME " WHERE createTxId=?1 AND confirmHeight=?2;";
            sqlite::Statement stm(this, req);
            stm.bind(1, txId);
            stm.bind(2, MaxHeight);
            while (stm.step())
            {
                Coin& coin = deletedItems.emplace_back();
                int colIdx = 0;
                ENUM_ALL_STORAGE_FIELDS(STM_GET_LIST, NOSEP, coin);
            }
        }
        if (!deletedItems.empty())
        {
            const char* req = "DELETE FROM " STORAGE_NAME " WHERE createTxId=?1 AND confirmHeight=?2;";
            sqlite::Statement stm(this, req);
            stm.bind(1, txId);
            stm.bind(2, MaxHeight);
            stm.step();

            notifyCoinsChanged(ChangeAction::Removed, deletedItems);
        }
    }

    void WalletDB::restoreShieldedCoinsSpentByTx(const TxID& txId)
    {
        std::vector<ShieldedTxo::BaseKey> updatedRows;
        {
            const char* req = "SELECT Key FROM " SHIELDED_COINS_NAME " WHERE spentTxId=?1;";
            sqlite::Statement stm(this, req);
            stm.bind(1, txId);
            while (stm.step())
            {
                ShieldedTxo::BaseKey& key = updatedRows.emplace_back();
                stm.get(0, key);
            }
        }
        if (!updatedRows.empty())
        {
            {
                const char* req = "UPDATE " SHIELDED_COINS_NAME " SET spentTxId=NULL WHERE spentTxId=?1;";
                sqlite::Statement stm(this, req);
                stm.bind(1, txId);
                stm.step();
            }

            vector<ShieldedCoin> updatedCoins;
            updatedCoins.reserve(updatedRows.size());

            auto currentHeight = getCurrentHeight();
            for (const auto& key : updatedRows)
            {
                auto& coin = updatedCoins.emplace_back(getShieldedCoin(key).value());
                storage::DeduceStatus(*this, coin, currentHeight);
            }

            notifyShieldedCoinsChanged(ChangeAction::Updated, updatedCoins);
        }
    }

    void WalletDB::DeleteShieldedCoin(const ShieldedTxo::BaseKey& key)
    {
        const char* req = "DELETE FROM " SHIELDED_COINS_NAME " WHERE Key=?1;";
        sqlite::Statement stm(this, req);
        stm.bind(1, key);
        stm.step();
    }

    void WalletDB::deleteShieldedCoinsCreatedByTx(const TxID& txId)
    {
        ShieldedStatusCtx ssc(*this);
        std::vector<ShieldedCoin> deletedItems;
        {
            const char* req = "SELECT " ENUM_SHIELDED_COIN_FIELDS(LIST, COMMA, ) " FROM " SHIELDED_COINS_NAME " WHERE createTxId=?1 AND confirmHeight=?2;";
            sqlite::Statement stm(this, req);
            stm.bind(1, txId);
            stm.bind(2, MaxHeight);
            while (stm.step())
            {
                ShieldedCoin& coin = deletedItems.emplace_back();
                int colIdx = 0;
                ENUM_SHIELDED_COIN_FIELDS(STM_GET_LIST, NOSEP, coin);
                storage::DeduceStatus(*this, coin, ssc.m_hTip);
                DeleteShieldedCoin(coin.m_CoinID.m_Key);
            }
        }

        if (!deletedItems.empty())
            notifyShieldedCoinsChanged(ChangeAction::Removed, deletedItems);
    }

    namespace
    {
        void loadAddress(const IWalletDB* db, sqlite::Statement& stm, WalletAddress& address)
        {
            int colIdx = 0;
            ENUM_ADDRESS_FIELDS(STM_GET_LIST, NOSEP, address);
            if (address.isOwn() && address.m_Identity == Zero)
                db->get_Identity(address.m_Identity, address.m_OwnID);
        }
    }

    boost::optional<WalletAddress> WalletDB::getAddressByToken(const std::string& token, bool isLaser) const
    {
        const std::string addrTableName = isLaser ? LASER_ADDRESSES_NAME : ADDRESSES_NAME;
        auto req = "SELECT * FROM " + addrTableName + " WHERE Address=?1;";

        sqlite::Statement stm(this, req.c_str());
        stm.bind(1, token);

        if (stm.step())
        {
            WalletAddress address = {};
            loadAddress(this, stm, address);

            return address;
        }

        return boost::optional<WalletAddress>();
    }

    boost::optional<WalletAddress> WalletDB::getAddress(const WalletID& id, bool isLaser) const
    {
        const std::string addrTableName = isLaser ? LASER_ADDRESSES_NAME : ADDRESSES_NAME;
        auto req = "SELECT * FROM " + addrTableName + " WHERE walletID=?1;";

        sqlite::Statement stm(this, req.c_str());
        stm.bind(1, id);

        if (stm.step())
        {
            WalletAddress address = {};
            loadAddress(this, stm, address);

            return address;
        }

        return boost::optional<WalletAddress>();
    }

    std::vector<WalletAddress> WalletDB::getAddresses(bool own, bool isLaser) const
    {
        const std::string addrTableName =
            isLaser ? LASER_ADDRESSES_NAME : ADDRESSES_NAME;
        vector<WalletAddress> res;
        auto req = "SELECT * FROM " + addrTableName + " ORDER BY createTime DESC;";
        sqlite::Statement stm(this, req.c_str());

        while (stm.step())
        {
            auto& a = res.emplace_back();
            loadAddress(this, stm, a);

            if (a.isOwn() != own)
                res.pop_back(); // akward, but ok
        }
        return res;
    }

    void WalletDB::saveAddress(const WalletAddress& addr, bool isLaser)
    {
        const std::string addrTableName = isLaser ? LASER_ADDRESSES_NAME : ADDRESSES_NAME;

        WalletAddress address = addr;
        if (address.m_Address.empty())
        {
            assert(address.m_walletID != Zero);
            address.m_Address = std::to_string(address.m_walletID);
        }

        auto selectByToken = "SELECT * FROM " + addrTableName + " WHERE address=?1;";
        sqlite::Statement stmToken(this, selectByToken.c_str());
        stmToken.bind(1, address.m_Address);

        if (stmToken.step())
        {
            {
                auto updateReq = "UPDATE " + addrTableName + " SET label=?2, category=?3, duration=?4, createTime=?5, walletID=?6, OwnID=?7, Identity=?8 WHERE address=?1;";
                sqlite::Statement stm(this, updateReq.c_str());

                stm.bind(1, address.m_Address);
                stm.bind(2, address.m_label);
                stm.bind(3, address.m_category);
                stm.bind(4, address.m_duration);
                stm.bind(5, address.m_createTime);
                stm.bind(6, address.m_walletID);
                stm.bind(7, address.m_OwnID);
                stm.bind(8, address.m_Identity);
                stm.step();
            }

            if (!isLaser)
            {
                notifyAddressChanged(ChangeAction::Updated, { address });
            }

            return;
        }

        auto selectByWid = "SELECT * FROM " + addrTableName + " WHERE walletID=?1;";
        sqlite::Statement stmWid(this, selectByWid.c_str());
        stmWid.bind(1, address.m_walletID);

        if (stmWid.step())
        {
            {
                auto updateReq = "UPDATE " + addrTableName + " SET address=?1, label=?2, category=?3, duration=?4, createTime=?5, OwnID=?7, Identity=?8 WHERE walletID=?6;";
                sqlite::Statement stm(this, updateReq.c_str());

                stm.bind(1, address.m_Address);
                stm.bind(2, address.m_label);
                stm.bind(3, address.m_category);
                stm.bind(4, address.m_duration);
                stm.bind(5, address.m_createTime);
                stm.bind(6, address.m_walletID);
                stm.bind(7, address.m_OwnID);
                stm.bind(8, address.m_Identity);
                stm.step();
            }

            if (!isLaser)
            {
                notifyAddressChanged(ChangeAction::Updated, {address});
            }

            return;
        }

        {
            auto insertReq = "INSERT INTO " + addrTableName + " (" ENUM_ADDRESS_FIELDS(LIST, COMMA, ) ") VALUES(" ENUM_ADDRESS_FIELDS(BIND_LIST, COMMA, ) ");";
            sqlite::Statement stm(this, insertReq.c_str());
            int colIdx = 0;
            ENUM_ADDRESS_FIELDS(STM_BIND_LIST, NOSEP, address);
            stm.step();
        }

        if (!isLaser)
        {
            notifyAddressChanged(ChangeAction::Added, { address });
        }
    }

    void WalletDB::generateAndSaveDefaultAddress()
    {
        WalletAddress address;
        createAddress(address);
        address.m_label = kDefaultAddrLabel;
        address.setExpirationStatus(WalletAddress::ExpirationStatus::Never);
        saveAddress(address);
        LOG_DEBUG() << "Default address: " << std::to_string(address.m_walletID);
    }

    void WalletDB::deleteAddress(const WalletID& id, bool isLaser)
    {
        auto address = getAddress(id, isLaser);
        assert(address.is_initialized());

        if (address)
        {
            const std::string addrTableName = isLaser ? LASER_ADDRESSES_NAME : ADDRESSES_NAME;
            const auto req = "DELETE FROM " + addrTableName + " WHERE walletID=?1;";
            sqlite::Statement stm(this, req.c_str());

            stm.bind(1, id);
            stm.step();

            if (!isLaser)
            {
                notifyAddressChanged(ChangeAction::Removed, {*address});
            }
        }
    }

    void WalletDB::saveAsset(const Asset::Full &info, Height refreshHeight)
    {
        refreshHeight = refreshHeight ? refreshHeight : getCurrentHeight();
        assert(info.m_LockHeight <= refreshHeight);

        const char* find = "SELECT * FROM " ASSETS_NAME " WHERE ID=?1;";
        sqlite::Statement stmFind(this, find);
        stmFind.bind(1, info.m_ID);

        const char* update = "UPDATE " ASSETS_NAME " SET Value=?2, Owner=?3, LockHeight=?4, Metadata=?5, RefreshHeight=?6 WHERE ID=?1;";
        const char* insert = "INSERT INTO " ASSETS_NAME " (" ENUM_ASSET_FIELDS(LIST, COMMA, ) ") VALUES(" ENUM_ASSET_FIELDS(BIND_LIST, COMMA, ) ");";
        const bool  found  = stmFind.step();

        sqlite::Statement stm(this, found ? update: insert);
        stm.bind(1, info.m_ID);
        stm.bind(2, info.m_Value);
        stm.bind(3, info.m_Owner);
        stm.bind(4, info.m_LockHeight);
        stm.bind(5, info.m_Metadata.m_Value);
        stm.bind(6, refreshHeight);

        if (!found)
        {
            // By default we do not mark new assets as owned
            // For owned assets this value is set to the owner index by the 'AssetRegister' transaction
            stm.bind(7, 0);
        }

        stm.step();
    }

    void WalletDB::markAssetOwned(const Asset::ID assetId)
    {
        const char* update = "UPDATE " ASSETS_NAME " SET IsOwned=?2 WHERE ID=?1;";
        sqlite::Statement stmUpdate(this, update);
        stmUpdate.bind(1, assetId);
        stmUpdate.bind(2, 1);
        stmUpdate.step();
    }

    void WalletDB::dropAsset(const PeerID& ownerId)
    {
        if(const auto oasset = findAsset(ownerId))
        {
            const auto& info = *oasset;
            dropAsset(info.m_ID);
        }
    }

    void WalletDB::dropAsset(const Asset::ID assetId)
    {
        const char* deleteReq = "DELETE FROM " ASSETS_NAME " WHERE ID=?1;";
        sqlite::Statement stm(this, deleteReq);
        stm.bind(1, assetId);
        stm.step();
    }

    void WalletDB::visitAssets(std::function<bool(const WalletAsset& info)> visitor) const
    {
        const char* select = "SELECT * FROM " ASSETS_NAME " ORDER BY ID;";
        sqlite::Statement stm(this, select);

        while (stm.step())
        {
            WalletAsset asset;

            stm.get(0, asset.m_ID);
            stm.get(1, asset.m_Value);
            stm.get(2, asset.m_Owner);
            stm.get(3, asset.m_LockHeight);
            stm.get(4, asset.m_Metadata.m_Value);
            asset.m_Metadata.UpdateHash();
            stm.get(5, asset.m_RefreshHeight);
            assert(asset.m_RefreshHeight != 0);
            stm.get(6, asset.m_IsOwned);

            if (!visitor(asset))
            {
                break;
            }
        }
    }

    boost::optional<WalletAsset> WalletDB::findAsset(Asset::ID assetId) const
    {
        const char* find = "SELECT * FROM " ASSETS_NAME " WHERE ID=?1;";
        sqlite::Statement stmFind(this, find);
        stmFind.bind(1, assetId);
        if (!stmFind.step())
        {
            return boost::optional<WalletAsset>();
        }

        WalletAsset asset;

        stmFind.get(0, asset.m_ID);
        assert(asset.m_ID == assetId);

        stmFind.get(1, asset.m_Value);
        stmFind.get(2, asset.m_Owner);
        stmFind.get(3, asset.m_LockHeight);
        stmFind.get(4, asset.m_Metadata.m_Value);
        asset.m_Metadata.UpdateHash();
        stmFind.get(5, asset.m_RefreshHeight);
        assert(asset.m_RefreshHeight != 0);
        stmFind.get(6, asset.m_IsOwned);

        return asset;
    }

    boost::optional<WalletAsset> WalletDB::findAsset(const PeerID& ownerID) const
    {
        const char* find = "SELECT ID FROM " ASSETS_NAME " WHERE Owner=?1;";
        sqlite::Statement stmFind(this, find);
        stmFind.bind(1, ownerID);

        if (!stmFind.step())
        {
            return boost::optional<WalletAsset>();
        }

        Asset::ID assetID = Asset::s_InvalidID;
        stmFind.get(0, assetID);

        return findAsset(assetID);
    }

    void WalletDB::rollbackAssets(Height minHeight)
    {
        const char* drop = "DELETE FROM " ASSETS_NAME " WHERE LockHeight>?1;";
        sqlite::Statement stmDrop(this, drop);
        stmDrop.bind(1, minHeight);
        stmDrop.step();

        const char* update = "UPDATE " ASSETS_NAME " SET RefreshHeight=?1 WHERE RefreshHeight > ?1;";
        sqlite::Statement stmUpdate(this, update);
        stmUpdate.bind(1, minHeight);
        stmUpdate.step();
    }

    void WalletDB::saveLaserChannel(const ILaserChannelEntity& ch)
    {
        const auto& channelID = ch.get_chID();
        LOG_DEBUG() << "Save channel: "
                    << to_hex(channelID->m_pData, channelID->nBytes);
        const char* selectReq = "SELECT * FROM " LASER_CHANNELS_NAME " WHERE chID=?1;";
        sqlite::Statement stm2(this, selectReq);
        stm2.bind(1, channelID->m_pData, channelID->nBytes);

        if (stm2.step())
        {
            const char* updateReq = "UPDATE " LASER_CHANNELS_NAME " SET myWID=?2, trgWID=?3, state=?4, fee=?5, locktime=?6, amountMy=?7, amountTrg=?8, amountCurrentMy=?9, amountCurrentTrg=?10, lockHeight=?11, bbsTimestamp=?12, data=?13 WHERE chID=?1;";
            sqlite::Statement stm(this, updateReq);

            stm.bind(1, channelID->m_pData, channelID->nBytes);
            stm.bind(2, ch.get_myWID());
            stm.bind(3, ch.get_trgWID());
            stm.bind(4, ch.get_State());
            stm.bind(5, ch.get_fee());
            stm.bind(6, ch.getLocktime());
            stm.bind(7, ch.get_amountMy());
            stm.bind(8, ch.get_amountTrg());
            stm.bind(9, ch.get_amountCurrentMy());
            stm.bind(10, ch.get_amountCurrentTrg());
            stm.bind(11, ch.get_LockHeight());
            stm.bind(12, ch.get_BbsTimestamp());
            stm.bind(13, ch.get_Data().data(), ch.get_Data().size());
            stm.step();
        }
        else
        {
            const char* insertReq = "INSERT INTO " LASER_CHANNELS_NAME " (" ENUM_LASER_CHANNEL_FIELDS(LIST, COMMA, ) ") VALUES(" ENUM_LASER_CHANNEL_FIELDS(BIND_LIST, COMMA, ) ");";
            sqlite::Statement stm(this, insertReq);
            stm.bind(1, channelID->m_pData, channelID->nBytes);
            stm.bind(2, ch.get_myWID());
            stm.bind(3, ch.get_trgWID());
            stm.bind(4, ch.get_State());
            stm.bind(5, ch.get_fee());
            stm.bind(6, ch.getLocktime());
            stm.bind(7, ch.get_amountMy());
            stm.bind(8, ch.get_amountTrg());
            stm.bind(9, ch.get_amountCurrentMy());
            stm.bind(10, ch.get_amountCurrentTrg());
            stm.bind(11, ch.get_LockHeight());
            stm.bind(12, ch.get_BbsTimestamp());
            stm.bind(13, ch.get_Data().data(), ch.get_Data().size());
            stm.step();

            saveAddress(ch.get_myAddr(), true);
        }

        flushDB();
    }

    bool WalletDB::getLaserChannel(const std::shared_ptr<uintBig_t<16>>& chId,
                                   TLaserChannelEntity* entity)
    {
        const char* selectReq = "SELECT " LASER_CHANNEL_FIELDS " FROM " LASER_CHANNELS_NAME " WHERE chID=?1;";
        sqlite::Statement stm(this, selectReq);
        stm.bind(1, chId->m_pData, chId->nBytes);

        if (stm.step())
        {
            stm.get(LaserFields::LASER_CH_ID, std::get<LaserFields::LASER_CH_ID>(*entity));
            stm.get(LaserFields::LASER_MY_WID, std::get<LaserFields::LASER_MY_WID>(*entity));
            stm.get(LaserFields::LASER_TRG_WID, std::get<LaserFields::LASER_TRG_WID>(*entity));
            stm.get(LaserFields::LASER_STATE, std::get<LaserFields::LASER_STATE>(*entity));
            stm.get(LaserFields::LASER_FEE, std::get<LaserFields::LASER_FEE>(*entity));
            stm.get(LaserFields::LASER_LOCKTIME, std::get<LaserFields::LASER_LOCKTIME>(*entity));
            stm.get(LaserFields::LASER_AMOUNT_MY, std::get<LaserFields::LASER_AMOUNT_MY>(*entity));
            stm.get(LaserFields::LASER_AMOUNT_TRG, std::get<LaserFields::LASER_AMOUNT_TRG>(*entity));
            stm.get(LaserFields::LASER_AMOUNT_CURRENT_MY, std::get<LaserFields::LASER_AMOUNT_CURRENT_MY>(*entity));
            stm.get(LaserFields::LASER_AMOUNT_CURRENT_TRG, std::get<LaserFields::LASER_AMOUNT_CURRENT_TRG>(*entity));
            stm.get(LaserFields::LASER_LOCK_HEIGHT, std::get<LaserFields::LASER_LOCK_HEIGHT>(*entity));
            stm.get(LaserFields::LASER_BBS_TIMESTAMP, std::get<LaserFields::LASER_BBS_TIMESTAMP>(*entity));
            stm.get(LaserFields::LASER_DATA, std::get<LaserFields::LASER_DATA>(*entity));
            return true;
        }
        return false;
    }

    bool WalletDB::removeLaserChannel(
            const std::shared_ptr<uintBig_t<16>>& chId)
    {
        LOG_INFO() << "Removing channel: "
                   << to_hex(chId->m_pData, chId->nBytes);

        const char* selectReq = "SELECT chID, myWID FROM " LASER_CHANNELS_NAME " WHERE chID=?1;";
        sqlite::Statement selectStm(this, selectReq);
        selectStm.bind(1, chId->m_pData, chId->nBytes);

        try
        {
            if (selectStm.step())
            {
                uintBig_t<16> checkChId;
                WalletID myWID;
                selectStm.get(0, checkChId);
                selectStm.get(1, myWID);
                if (*chId == checkChId)
                {
                    const char* deleteReq = "DELETE FROM " LASER_CHANNELS_NAME " WHERE chID=?1;";
                    sqlite::Statement stm(this, deleteReq);
                    stm.bind(1, chId->m_pData, chId->nBytes);

                    stm.step();
                    deleteAddress(myWID, true);
                    return true;
                }
            }
        }
        catch (const runtime_error&)
        {
        }

        return false;
    }

    std::vector<TLaserChannelEntity> WalletDB::loadLaserChannels(uint8_t state)
    {
        std::vector<TLaserChannelEntity> channels;

        sqlite::Statement countStm(this, "SELECT COUNT(*) FROM " LASER_CHANNELS_NAME ";");
        countStm.step();
        
        uint64_t count;
        countStm.get(0, count);
        channels.reserve(count);

        sqlite::Statement stm(
            this, 
            state
                ? "SELECT * FROM " LASER_CHANNELS_NAME " WHERE state=?1;"
                : "SELECT * FROM " LASER_CHANNELS_NAME ";");
        if (state)
            stm.bind(1, state);

        while (stm.step())
        {
            auto& entity = channels.emplace_back();

            stm.get(LaserFields::LASER_CH_ID, std::get<LaserFields::LASER_CH_ID>(entity));
            stm.get(LaserFields::LASER_MY_WID, std::get<LaserFields::LASER_MY_WID>(entity));
            stm.get(LaserFields::LASER_TRG_WID, std::get<LaserFields::LASER_TRG_WID>(entity));
            stm.get(LaserFields::LASER_STATE, std::get<LaserFields::LASER_STATE>(entity));
            stm.get(LaserFields::LASER_FEE, std::get<LaserFields::LASER_FEE>(entity));
            stm.get(LaserFields::LASER_LOCKTIME, std::get<LaserFields::LASER_LOCKTIME>(entity));
            stm.get(LaserFields::LASER_AMOUNT_MY, std::get<LaserFields::LASER_AMOUNT_MY>(entity));
            stm.get(LaserFields::LASER_AMOUNT_TRG, std::get<LaserFields::LASER_AMOUNT_TRG>(entity));
            stm.get(LaserFields::LASER_AMOUNT_CURRENT_MY, std::get<LaserFields::LASER_AMOUNT_CURRENT_MY>(entity));
            stm.get(LaserFields::LASER_AMOUNT_CURRENT_TRG, std::get<LaserFields::LASER_AMOUNT_CURRENT_TRG>(entity));
            stm.get(LaserFields::LASER_LOCK_HEIGHT, std::get<LaserFields::LASER_LOCK_HEIGHT>(entity));
            stm.get(LaserFields::LASER_BBS_TIMESTAMP, std::get<LaserFields::LASER_BBS_TIMESTAMP>(entity));
            stm.get(LaserFields::LASER_DATA, std::get<LaserFields::LASER_DATA>(entity));
        }

        return channels;
    }

    vector<Notification> WalletDB::getNotifications() const
    {
        vector<Notification> res;
        const char* req = "SELECT * FROM " NOTIFICATIONS_NAME " ORDER BY createTime DESC;";
        sqlite::Statement stm(this, req);

        while (stm.step())
        {
            auto& notification = res.emplace_back();
            int colIdx = 0;
            ENUM_NOTIFICATION_FIELDS(STM_GET_LIST, NOSEP, notification);
        }
        return res;
    }

    void WalletDB::saveNotification(const Notification& notification)
    {
        const char* selectReq = "SELECT * FROM " NOTIFICATIONS_NAME " WHERE ID=?1;";
        sqlite::Statement selectStm(this, selectReq);
        selectStm.bind(1, notification.m_ID);

        if (selectStm.step())
        {
            const char* updateReq = "UPDATE " NOTIFICATIONS_NAME " SET type=?2, state=?3, createTime=?4, content=?5 WHERE ID=?1;";
            sqlite::Statement updateStm(this, updateReq);

            updateStm.bind(1, notification.m_ID);
            updateStm.bind(2, notification.m_type);
            updateStm.bind(3, notification.m_state);
            updateStm.bind(4, notification.m_createTime);
            updateStm.bind(5, notification.m_content);
            updateStm.step();
        }
        else
        {
            const char* insertReq = "INSERT INTO " NOTIFICATIONS_NAME " (" ENUM_NOTIFICATION_FIELDS(LIST, COMMA, ) ") VALUES(" ENUM_NOTIFICATION_FIELDS(BIND_LIST, COMMA, ) ");";
            sqlite::Statement stm(this, insertReq);
            int colIdx = 0;
            ENUM_NOTIFICATION_FIELDS(STM_BIND_LIST, NOSEP, notification);
            stm.step();
        }
    }

    std::vector<ExchangeRate> WalletDB::getLatestExchangeRates() const
    {
        ExchangeRates res;

        const char* req = "SELECT * FROM " EXCHANGE_RATES_NAME " ORDER BY updateTime DESC;";
        sqlite::Statement stm(this, req);

        while (stm.step())
        {
            auto& rate = res.emplace_back();
            int colIdx = 0;
            ENUM_EXCHANGE_RATES_FIELDS(STM_GET_LIST, NOSEP, rate);
        }

        return res;
    }

    void WalletDB::saveExchangeRate(const ExchangeRate& rate)
    {
        const char* selectReq = "SELECT * FROM " EXCHANGE_RATES_NAME " WHERE cfrom=?1 AND cto=?2;";
        sqlite::Statement selectStm(this, selectReq);
        selectStm.bind(1, rate.m_from.m_value);
        selectStm.bind(2, rate.m_to.m_value);

        if (selectStm.step())
        {
            const char* updateReq = "UPDATE " EXCHANGE_RATES_NAME " SET rate=?1, updateTime=?2 WHERE cfrom=?3 AND cto=?4;";
            sqlite::Statement updateStm(this, updateReq);

            updateStm.bind(1, rate.m_rate);
            updateStm.bind(2, rate.m_updateTime);
            updateStm.bind(3, rate.m_from.m_value);
            updateStm.bind(4, rate.m_to.m_value);
            updateStm.step();
        }
        else
        {
            const char* insertReq = "INSERT INTO " EXCHANGE_RATES_NAME " (" ENUM_EXCHANGE_RATES_FIELDS(LIST, COMMA, ) ") VALUES(" ENUM_EXCHANGE_RATES_FIELDS(BIND_LIST, COMMA, ) ");";
            sqlite::Statement stm(this, insertReq);
            int colIdx = 0;
            ENUM_EXCHANGE_RATES_FIELDS(STM_BIND_LIST, NOSEP, rate);
            stm.step();
        }
    }

    boost::optional<ExchangeRateAtPoint> WalletDB::getExchangeRateNearPoint(const Currency& from, const Currency& to, uint64_t maxHeight) const
    {
        const char* req = "SELECT * FROM " EXCHANGE_RATES_HISTORY_NAME " WHERE cfrom=?1 AND cto=?2 AND height<=?3 ORDER BY updateTime DESC LIMIT 1;";
        sqlite::Statement stm(this, req);
        stm.bind(1, from.m_value);
        stm.bind(2, to.m_value);
        stm.bind(3, maxHeight);

        while (stm.step())
        {
            int colIdx = 0;
            ExchangeRateAtPoint rate;
            ENUM_EXCHANGE_RATES_HISTORY_FIELDS(STM_GET_LIST, NOSEP, rate);
            return rate;
        }

        return boost::none;
    }

    ExchangeRatesHistory WalletDB::getExchangeRatesHistory(uint64_t startHeight, uint64_t endHeight) const
    {
        ExchangeRatesHistory res;

        const char* req = "SELECT * FROM " EXCHANGE_RATES_HISTORY_NAME " WHERE height<=?1 AND height>=?2 ORDER BY updateTime DESC;";
        sqlite::Statement stm(this, req);
        stm.bind(1, endHeight);
        stm.bind(2, startHeight);

        while (stm.step())
        {
            auto& rate = res.emplace_back();
            int colIdx = 0;
            ENUM_EXCHANGE_RATES_HISTORY_FIELDS(STM_GET_LIST, NOSEP, rate);
        }

        return res;
    }

    void WalletDB::saveExchangeRate(const ExchangeRateAtPoint& rate)
    {
        const char* insertReq = "INSERT INTO " EXCHANGE_RATES_HISTORY_NAME " (" ENUM_EXCHANGE_RATES_HISTORY_FIELDS(LIST, COMMA, ) ") VALUES(" ENUM_EXCHANGE_RATES_HISTORY_FIELDS(BIND_LIST, COMMA, ) ");";
        sqlite::Statement stm(this, insertReq);
        int colIdx = 0;
        ENUM_EXCHANGE_RATES_HISTORY_FIELDS(STM_BIND_LIST, NOSEP, rate);
        stm.step();
    }

    boost::optional<ShieldedTxo::Voucher> WalletDB::grabVoucher(const WalletID& peerID)
    {
        int f = VoucherFlags::Deleted;
        boost::optional<ShieldedTxo::Voucher> res;
        sqlite::Statement stm(this, "SELECT rowid, Voucher, Flags FROM " VOUCHERS_NAME " WHERE WalletID=?1 AND (Flags&?2)!=?2 ;");
        stm.bind(1, peerID);
        stm.bind(2, f);

        if (stm.step())
        {
            res.emplace();
            int rowID = 0;
            stm.get(0, rowID);
            stm.get(1, *res);
            stm.get(2, f);
            
            if ((f & VoucherFlags::Preserve) == 0)
            {
                sqlite::Statement delStm(this, "DELETE FROM " VOUCHERS_NAME " WHERE rowid=?1;");
                delStm.bind(1, rowID);
                delStm.step();
            }
            else
            {
                f |= VoucherFlags::Deleted;
                sqlite::Statement stm2(this, "UPDATE " VOUCHERS_NAME  " SET Flags=?2 WHERE rowid=?1;");
                stm2.bind(1, rowID);
                stm2.bind(2, f);
                stm2.step();
            }
        }
        return res;
    }

    void WalletDB::saveVoucher(const ShieldedTxo::Voucher& v, const WalletID& walletID, bool preserveOnGrab)
    {
        uint16_t flags = (preserveOnGrab) ? VoucherFlags::Preserve : VoucherFlags::Default;
        sqlite::Statement stm(this, "INSERT INTO " VOUCHERS_NAME " (" ENUM_VOUCHERS_FIELDS(LIST, COMMA, ) ") VALUES(?,?,?);");
        stm.bind(1, walletID);
        stm.bind(2, v);
        stm.bind(3, flags);
        stm.step();
    }

    size_t WalletDB::getVoucherCount(const WalletID& peerID) const
    {
        int res = 0;
        uint16_t f = VoucherFlags::Deleted | VoucherFlags::Preserve;
        sqlite::Statement stm(this, "SELECT COUNT(Voucher) FROM " VOUCHERS_NAME " WHERE WalletID=?1 AND (Flags & ?2)!=?2;");
        stm.bind(1, peerID);
        stm.bind(2, f);
        if (stm.step())
        {
            stm.get(0, res);
        }
        return size_t(res);
    }

    void WalletDB::insertEvent(Height h, const Blob& body, const Blob& key)
    {
        sqlite::Statement stm(this, "INSERT INTO " EVENTS_NAME "(" ENUM_EVENTS_FIELDS(LIST, COMMA, ) ") VALUES(" ENUM_EVENTS_FIELDS(BIND_LIST, COMMA, ) ");");
        stm.bind(1, h);
        stm.bind(2, body);
        stm.bind(3, key);
        stm.step();
    }

    void WalletDB::deleteEventsFrom(Height h)
    {
        sqlite::Statement stm(this, "DELETE FROM " EVENTS_NAME " WHERE Height >= ?1");
        stm.bind(1, h);
        stm.step();
    }

    void WalletDB::visitEvents(Height min, const Blob& key, std::function<bool(Height, ByteBuffer&&)>&& func) const
    {
        sqlite::Statement stm(this, "SELECT * FROM " EVENTS_NAME " WHERE Height >= ?1 AND Key == ?2");
        stm.bind(1, min);
        stm.bind(2, key);
        while (stm.step())
        {
            Height h = 0;
            ByteBuffer body;
            stm.get(0, h);
            stm.get(1, body);
            if (!func(h, std::move(body)))
                break;
        }
    }

    void WalletDB::visitEvents(Height min, std::function<bool(Height, ByteBuffer&&)>&& func) const
    {
        sqlite::Statement stm(this, "SELECT * FROM " EVENTS_NAME " WHERE Height >= ?1");
        stm.bind(1, min);
        while (stm.step())
        {
            Height h = 0;
            ByteBuffer body;
            stm.get(0, h);
            stm.get(1, body);
            if (!func(h, std::move(body)))
                break;
        }
    }

    void WalletDB::Subscribe(IWalletDbObserver* observer)
    {
        if (std::find(m_subscribers.begin(), m_subscribers.end(), observer) == m_subscribers.end())
        {
            m_subscribers.push_back(observer);
        }
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

    bool WalletDB::setTxParameter(const TxID& txID, SubTxID subTxID, TxParameterID paramID, const ByteBuffer& blob, bool shouldNotifyAboutChanges, bool allowModify /* = true */)
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
                if (!allowModify)
                    return false;

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
                OnTxSummaryParam(txID, subTxID, paramID, &blob);
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
        OnTxSummaryParam(txID, subTxID, paramID, &blob);
        return true;
    }

    template<typename T>
    void WalletDB::FillTxSummaryTableParam(const char* szField, TxParameterID paramID)
    {
        sqlite::Statement stm(this, "SELECT txID, value FROM " TX_PARAMS_NAME " WHERE paramID=?1 AND subTxID=?2;");
        stm.bind(1, paramID);
        stm.bind(2, kDefaultSubTxID);

        ByteBuffer buf;

        while (stm.step())
        {
            TxID txID;
            stm.get(0, txID);
            stm.get(1, buf);

            OnTxSummaryParam<T>(txID, szField, &buf);
            buf.clear();
        }
    }

    void WalletDB::FillTxSummaryTable()
    {
        int ret = sqlite3_exec(_db, "CREATE INDEX IF NOT EXISTS TxParamsValueIndex ON " TX_PARAMS_NAME " (paramID,subTxID);", nullptr, nullptr, nullptr);
        throwIfError(ret, _db);

#define THE_MACRO(id, type) FillTxSummaryTableParam<type>(#id, TxParameterID::id);
        ENUM_TX_SUMMARY_FIELDS(THE_MACRO)
#undef THE_MACRO

        ret = sqlite3_exec(_db, "DROP INDEX TxParamsValueIndex", nullptr, nullptr, nullptr);
        throwIfError(ret, _db);
    }


    void WalletDB::OnTxSummaryParam(const TxID& txID, SubTxID subTxID, TxParameterID paramID, const ByteBuffer* pBlob)
    {
        if (kDefaultSubTxID != subTxID)
            return;

        switch (paramID)
        {
#define THE_MACRO(id, type) case TxParameterID::id: OnTxSummaryParam<type>(txID, #id, pBlob); break;
        ENUM_TX_SUMMARY_FIELDS(THE_MACRO)

        default:
            // suppress warning
            break;
#undef THE_MACRO
        }
    }

    template<typename T>
    void WalletDB::OnTxSummaryParam(const TxID& txID, const char* szName, const ByteBuffer* pBlob)
    {
        char szQuery[0x100];
        snprintf(szQuery, _countof(szQuery), "UPDATE " TX_SUMMARY_NAME " SET %s=?1 WHERE TxID=?2", szName);

        T val = {};
        {
            sqlite::Statement stm(this, szQuery);
            stm.bind(2, txID);

            if (pBlob)
            {
                fromByteBuffer(*pBlob, val);
                stm.bind(1, val);
            }
            else
            {
                stm.bind(1, nullptr);
            }

            stm.step();
        }
        
        if (!sqlite3_changes(_db) && pBlob)
        {
            snprintf(szQuery, _countof(szQuery), "INSERT INTO " TX_SUMMARY_NAME " (TxID,%s) VALUES(?1,?2)", szName);

            sqlite::Statement stm(this, szQuery);
            stm.bind(1, txID);
            stm.bind(2, val);

            stm.step();
        }
    }

    bool WalletDB::delTxParameter(const TxID& txID, SubTxID subTxID, TxParameterID paramID)
    {
        if (auto txIter = m_TxParametersCache.find(txID); txIter != m_TxParametersCache.end())
        {
            if (auto subTxIter = txIter->second.find(subTxID); subTxIter != txIter->second.end())
            {
                if (auto pit = subTxIter->second.find(paramID); pit != subTxIter->second.end())
                {
                    subTxIter->second.erase(pit);
                }
            }
        }

        sqlite::Statement stm(this, "DELETE FROM " TX_PARAMS_NAME " WHERE txID=?1 AND subTxID=?2 AND paramID=?3;");

        stm.bind(1, txID);
        stm.bind(2, subTxID);
        stm.bind(3, paramID);

        stm.step();

        OnTxSummaryParam(txID, subTxID, paramID, nullptr);

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

        sqlite::Statement stm(this, "SELECT value FROM " TX_PARAMS_NAME " WHERE txID=?1 AND subTxID=?2 AND paramID=?3;");
        return getTxParameterImpl(txID, subTxID, paramID, blob, stm);
    }

    bool WalletDB::getTxParameterImpl(const TxID& txID, SubTxID subTxID, TxParameterID paramID, ByteBuffer& blob, sqlite::Statement& stm) const
    {
        stm.Reset();
        stm.bind(1, txID);
        stm.bind(2, subTxID);
        stm.bind(3, paramID);

        if (stm.step())
        {
            stm.get(0, blob);
            insertParameterToCache(txID, subTxID, paramID, blob);
            return true;
        }
        insertParameterToCache(txID, subTxID, paramID, boost::optional<ByteBuffer>());
        return false;
    }

    std::vector<TxParameter> WalletDB::getAllTxParameters() const
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
        }

        onFlushTimer();
    }

    void WalletDB::rollbackDB()
    {
        if (m_IsFlushPending)
        {
            assert(m_FlushTimer);
            m_FlushTimer->cancel();
            m_IsFlushPending = false;
        }
        if (m_DbTransaction)
        {
            m_DbTransaction->rollback();
            m_DbTransaction.reset();
        }
    }

    void WalletDB::onModified()
    {
        if (!m_Initialized) // wallet db is opening or initializing, there could be no reactor to run timer
        {
            //onFlushTimer();
        }
        else if (!m_IsFlushPending)
        {
            if (!m_FlushTimer)
            {
                m_FlushTimer = io::Timer::create(io::Reactor::get_Current());
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

#ifdef __EMSCRIPTEN__
            MAIN_THREAD_ASYNC_EM_ASM(
                FS.syncfs(false, function() {});
            );
#endif
        }
    }

    void WalletDB::onPrepareToModify()
    {
        if (!m_DbTransaction)
        {
            m_DbTransaction.reset(new sqlite::Transaction(_db));
        }
    }

    void WalletDB::notifyCoinsChanged(ChangeAction action, const vector<Coin>& items)
    {
        if (items.empty() && action != ChangeAction::Reset)
            return;

        for (const auto sub : m_subscribers)
        {
            sub->onCoinsChanged(action, items);
        }
    }

    void WalletDB::notifyTransactionChanged(ChangeAction action, const vector<TxDescription>& items)
    {
        if (items.empty() && action != ChangeAction::Reset)
            return;

        for (const auto sub : m_subscribers)
        {
            sub->onTransactionChanged(action, items);
        }
    }

    void WalletDB::notifySystemStateChanged(const Block::SystemState::ID& stateID)
    {
        for (const auto sub : m_subscribers) sub->onSystemStateChanged(stateID);
    }

    void WalletDB::notifyAddressChanged(ChangeAction action, const vector<WalletAddress>& items)
    {
        if (items.empty() && action != ChangeAction::Reset)
            return;

        for (const auto sub : m_subscribers)
        {
            sub->onAddressChanged(action, items);
        }
    }

    void WalletDB::notifyShieldedCoinsChanged(ChangeAction action, const std::vector<ShieldedCoin>& items)
    {
        if (items.empty() && action != ChangeAction::Reset)
            return;

        for (const auto sub : m_subscribers)
        {
            sub->onShieldedCoinsChanged(action, items);
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

        bool getTxParameter(const IWalletDB& db, const TxID& txID, TxParameterID paramID, ByteBuffer& value)
        {
            return getTxParameter(db, txID, kDefaultSubTxID, paramID, value);
        }

        bool getTxParameter(const IWalletDB& db, const TxID& txID, TxParameterID paramID, ECC::Scalar::Native& value)
        {
            return getTxParameter(db, txID, kDefaultSubTxID, paramID, value);
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

                address->setExpirationStatus(status);
                walletDB.saveAddress(*address);
            }
            else
            {
                for (auto& address : walletDB.getAddresses(true))
                {
                    address.setExpirationStatus(status);
                    walletDB.saveAddress(address);
                }
            }
            return true;
        }

        Totals::Totals()
        {
            allTotals[Zero] = AssetTotals();
        }

        Totals::Totals(IWalletDB& db)
        {
            allTotals[Zero] = AssetTotals();
            Init(db);
        }

        void Totals::Init(IWalletDB& walletDB)
        {
            auto getTotalsRef = [this](Asset::ID assetId) -> AssetTotals& {
                if (allTotals.find(assetId) == allTotals.end()) {
                    allTotals[assetId] = AssetTotals();
                    allTotals[assetId].AssetId = assetId;
                }
                return allTotals[assetId];
            };

            walletDB.visitCoins([getTotalsRef] (const Coin& c) -> bool
            {
                auto& totals = getTotalsRef(c.m_ID.m_AssetID);
                totals.MinCoinHeightMW = totals.MinCoinHeightMW == 0 ? c.m_confirmHeight :
                                         std::min(c.m_confirmHeight, totals.MinCoinHeightMW);

                const AmountBig::Type value = c.m_ID.m_Value;
                switch (c.m_status)
                {
                case Coin::Status::Available:
                    totals.Avail   += value;
                    totals.Unspent += value;
                    switch (c.m_ID.m_Type)
                    {
                    case Key::Type::Coinbase:
                        assert(!c.isAsset());
                        totals.AvailCoinbase += value;
                        break;
                    case Key::Type::Comission:
                        assert(!c.isAsset());
                        totals.AvailFee += value;
                        break;
                    default: // suppress warning
                        break;
                    }
                    break;

                case Coin::Status::Maturing:
                    totals.Maturing += value;
                    totals.Unspent += value;
                    break;

                case Coin::Status::Incoming:
                    totals.Incoming += value;
                    if (c.m_ID.m_Type == Key::Type::Change)
                    {
                        totals.ReceivingChange += value;
                    }
                    else
                    {
                        totals.ReceivingIncoming += value;
                    }
                    break;

                case Coin::Status::Outgoing:
                    totals.Outgoing += value;
                    break;

                case Coin::Status::Unavailable:
                    totals.Unavail += value;
                    break;

                default: // suppress warning
                    break;
                }

                switch (c.m_ID.m_Type)
                {
                case Key::Type::Coinbase:
                    assert(!c.isAsset());
                    totals.Coinbase += value;
                    break;
                case Key::Type::Comission:
                    assert(!c.isAsset());
                    totals.Fee += value;
                    break;
                default: // suppress warning
                    break;
                }
                return true;
            });

            walletDB.visitShieldedCoins([getTotalsRef](const ShieldedCoin& c) -> bool {
                auto& totals = getTotalsRef(c.m_CoinID.m_AssetID);
                totals.MinCoinHeightShielded = totals.MinCoinHeightShielded == 0 ? c.m_confirmHeight :
                                               std::min(c.m_confirmHeight, totals.MinCoinHeightShielded);

                const AmountBig::Type value = c.m_CoinID.m_Value;
                switch(c.m_Status) {
                    case ShieldedCoin::Status::Available:
                        totals.AvailShielded += value;
                        totals.UnspentShielded += value;
                        break;
                    case ShieldedCoin::Status::Maturing:
                        totals.MaturingShielded += value;
                        totals.UnspentShielded += value;
                        break;
                    case ShieldedCoin::Status::Unavailable:
                        totals.UnavailShielded += value;
                        break;
                    case ShieldedCoin::Status::Outgoing:
                        totals.OutgoingShielded += value;
                        break;
                    case ShieldedCoin::Status::Incoming:
                        totals.IncomingShielded += value;
                        break;
                    case ShieldedCoin::Status::Spent:
                        break; // this is not necessary
                    default:
                        assert(false); // should never happen
                }
                return true;
            });

             walletDB.visitAssets([this](const WalletAsset& asset) -> bool {
                // we also add owned assets to totals even if there are no coins for owned assets
                if(!HasTotals(asset.m_ID) && asset.m_IsOwned)
                {
                    allTotals[asset.m_ID] = AssetTotals();
                    allTotals[asset.m_ID].AssetId = asset.m_ID;
                }
                return true;
             });
        }

        Totals::AssetTotals Totals::GetTotals(Asset::ID assetId) const
        {
            if(allTotals.find(assetId) == allTotals.end())
            {
                AssetTotals result;
                result.AssetId = assetId;
                return result;
            }
            return allTotals[assetId];
        }

        bool Totals::HasTotals(Asset::ID assetId) const
        {
            return allTotals.find(assetId) != allTotals.end();
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

        bool IsConsumeTx(const IWalletDB& walletDB, const boost::optional<TxID>& txID)
        {
            if (!txID) return false;

            TxType txType;
            if (getTxParameter(walletDB, txID.get(), TxParameterID::TransactionType, txType))
            {
                return txType == TxType::AssetConsume;
            }

            return false;
        }

        void DeduceStatus(const IWalletDB& walletDB, Coin& c, Height hTop)
        {
            if (c.m_spentHeight != MaxHeight)
            {
                if (c.isAsset() && IsConsumeTx(walletDB, c.m_spentTxId))
                {
                    c.m_status = Coin::Status::Consumed;
                }
                else
                {
                    c.m_status = Coin::Status::Spent;
                }
                return;
            }

            if (c.m_confirmHeight != MaxHeight)
            {
                if (hTop - walletDB.getCoinConfirmationsOffset() < c.m_maturity)
                {
                    c.m_status = Coin::Status::Maturing;
                    return;
                }

                if (IsOngoingTx(walletDB, c.m_spentTxId))
                {
                    c.m_status = Coin::Status::Outgoing;
                    return;
                }

                c.m_status = Coin::Status::Available;
                return;
            }

            if (IsOngoingTx(walletDB, c.m_createTxId))
            {
                c.m_status = Coin::Status::Incoming;
                return;
            }

            c.m_status = Coin::Status::Unavailable;
        }

        void DeduceStatus(const IWalletDB& walletDB, ShieldedCoin& c, Height hTop)
        {
            if (c.m_spentHeight != MaxHeight)
            {
                if (c.IsAsset() && IsConsumeTx(walletDB, c.m_spentTxId))
                {
                    c.m_Status = ShieldedCoin::Status::Consumed;
                }
                else
                {
                    c.m_Status = ShieldedCoin::Status::Spent;
                }
                return;
            }

            if (c.m_confirmHeight != MaxHeight)
            {
                const auto* packedMessage = ShieldedTxo::User::ToPackedMessage(c.m_CoinID.m_User);
                auto mpAnonymitySet = packedMessage->m_MaxPrivacyMinAnonymitySet;
                if (mpAnonymitySet)
                {
                    auto timeLimit = walletDB.get_MaxPrivacyLockTimeLimitHours();
                    Block::SystemState::ID stateID = {};
                    walletDB.getSystemStateID(stateID);

                    if (!timeLimit || c.m_confirmHeight + timeLimit * 60 > stateID.m_Height)
                    {
                        ShieldedCoin::UnlinkStatus unlinkStatus;
                        unlinkStatus.Init(c, walletDB.get_ShieldedOuts());
                        if (unlinkStatus.m_Progress < 100 * mpAnonymitySet / 64U)
                        {
                            c.m_Status = ShieldedCoin::Status::Maturing;
                            return;
                        }
                    }
                }

                if (hTop - c.m_confirmHeight < walletDB.getCoinConfirmationsOffset())
                {
                    c.m_Status = ShieldedCoin::Status::Maturing;
                    return;
                }

                if(storage::IsOngoingTx(walletDB, c.m_spentTxId))
                {
                    c.m_Status = ShieldedCoin::Status::Outgoing;
                    return;
                }

                c.m_Status = ShieldedCoin::Status::Available;
                return;
            }

            if (storage::IsOngoingTx(walletDB, c.m_createTxId))
            {
                c.m_Status = ShieldedCoin::Status::Incoming;
                return;
            }

            c.m_Status = ShieldedCoin::Status::Unavailable;
        }

        Height DeduceTxProofHeight(const IWalletDB& walletDB, const TxDescription &tx)
        {
            return DeduceTxProofHeightImpl(walletDB, tx.m_txId, tx.m_txType);
        }

        Height DeduceTxDisplayHeight(const IWalletDB& walletDB, const TxDescription &tx)
        {
            auto height = DeduceTxProofHeight(walletDB, tx);
            if (height == 0)
            {
                storage::getTxParameter(walletDB, tx.m_txId, TxParameterID::KernelUnconfirmedHeight, height);
                if (height == 0)
                {
                    storage::getTxParameter(walletDB, tx.m_txId, TxParameterID::AssetUnconfirmedHeight, height);
                    if (height == 0)
                    {
                        storage::getTxParameter(walletDB, tx.m_txId, TxParameterID::MinHeight, height);
                    }
                }
            }
            return height;
        }

        bool isTreasuryHandled(const IWalletDB& db)
        {
            bool res = false;
            storage::getVar(db, kIsTreasuryHandled, res);
            return res;
        }

        void setTreasuryHandled(IWalletDB& db,  bool value)
        {
            storage::setVar(db, kIsTreasuryHandled, value);
        }

        bool needToRequestBodies(const IWalletDB& db)
        {
            bool res = false;
            storage::getVar(db, kNeedToRequestBodies, res);
            return res;
        }

        void setNeedToRequestBodies(IWalletDB& db, bool value)
        {
            storage::setVar(db, kNeedToRequestBodies, value);
        }

        Height getNextEventHeight(const IWalletDB& db)
        {
            uintBigFor<Height>::Type var;
            if (!storage::getVar(db, s_szNextEvt, var))
                return 0;

            Height h;
            var.Export(h);
            return h;
        }

        void setNextEventHeight(IWalletDB& db, Height value)
        {
            storage::setVar(db, s_szNextEvt, uintBigFor<Height>::Type(value));
        }

        void updateCurrentStateWithTip(IWalletDB& db)
        {
            Block::SystemState::Full sTip;
            db.get_History().get_Tip(sTip);

            Block::SystemState::ID id;
            if (sTip.m_Height)
                sTip.get_ID(id);
            else
                ZeroObject(id);

            db.setSystemStateID(id);
            LOG_INFO() << "Current state is " << id;
        }

        namespace
        {
            Timestamp RestoreCreationTime(const Block::SystemState::Full& tip, Height confirmHeight)
            {
                Timestamp ts = tip.m_TimeStamp;
                if (tip.m_Height > confirmHeight)
                {
                    auto delta = (tip.m_Height - confirmHeight);
                    ts -= delta * Rules::get().DA.Target_s;
                }
                else if (tip.m_Height < confirmHeight)
                {
                    auto delta = confirmHeight - tip.m_Height;
                    ts += delta * Rules::get().DA.Target_s;
                }
                return ts;
            }
        }

        void restoreTransactionFromShieldedCoin(IWalletDB& db, ShieldedCoin& coin, INegotiatorGateway& gateway)
        {
            // add virtual transaction for receiver
            beam::Block::SystemState::Full tip;
            db.get_History().get_Tip(tip);
            storage::DeduceStatus(db, coin, tip.m_Height);

            if (coin.m_Status != ShieldedCoin::Status::Available &&
                coin.m_Status != ShieldedCoin::Status::Maturing &&
                coin.m_Status != ShieldedCoin::Status::Spent)
            {
                return;
            }

            const auto* message = ShieldedTxo::User::ToPackedMessage(coin.m_CoinID.m_User);
            TxID txID;
            std::copy_n(message->m_TxID.m_pData, 16, txID.begin());

            TxAddressType addressType = TxAddressType::Offline;
            if (message->m_MaxPrivacyMinAnonymitySet)
            {
                addressType = TxAddressType::MaxPrivacy;
            }
            else if (!coin.m_CoinID.m_Key.m_IsCreatedByViewer)
            {
                addressType = TxAddressType::PublicOffline;
            }

            auto tx = db.getTx(txID);
            if (tx)
            {
                storage::setTxParameter(db, txID, TxParameterID::AddressType, addressType, true);
                storage::setTxParameter(db, txID, TxParameterID::KernelProofHeight, coin.m_confirmHeight, true);
                return;
            }
            else
            {
                WalletAddress receiverAddress;
                if (message->m_ReceiverOwnID)
                {
                    db.get_SbbsWalletID(receiverAddress.m_walletID, message->m_ReceiverOwnID);
                    db.get_Identity(receiverAddress.m_Identity, message->m_ReceiverOwnID);
                }
                else
                {
                    // fake address
                    db.createAddress(receiverAddress);
                }

                auto params = CreateTransactionParameters(TxType::PushTransaction, txID)
                    .SetParameter(TxParameterID::MyID, receiverAddress.m_walletID)
                    .SetParameter(TxParameterID::PeerID, WalletID())
                    .SetParameter(TxParameterID::Status, TxStatus::Completed)
                    .SetParameter(TxParameterID::Amount, coin.m_CoinID.m_Value)
                    .SetParameter(TxParameterID::AssetID, coin.m_CoinID.m_AssetID)
                    .SetParameter(TxParameterID::IsSender, false)
                    .SetParameter(TxParameterID::CreateTime, RestoreCreationTime(tip, coin.m_confirmHeight))
                    .SetParameter(TxParameterID::PeerWalletIdentity, coin.m_CoinID.m_User.m_Sender)
                    .SetParameter(TxParameterID::MyWalletIdentity, receiverAddress.m_Identity)
                    .SetParameter(TxParameterID::KernelID, Merkle::Hash(Zero))
                    .SetParameter(TxParameterID::AddressType, addressType);

                const auto assetId = coin.m_CoinID.m_AssetID;
                if (assetId != Asset::s_BeamID)
                {
                    if (const auto oinfo = db.findAsset(assetId))
                    {
                        WalletAsset info(*oinfo);
                        if (info.IsExpired(db))
                        {
                            gateway.confirm_asset(txID, assetId);
                        }
                        else
                        {
                            params.SetParameter(TxParameterID::AssetInfoFull, static_cast<Asset::Full>(*oinfo))
                                    .SetParameter(TxParameterID::AssetConfirmedHeight, oinfo->m_RefreshHeight);
                        }
                    }
                    else
                    {
                        gateway.confirm_asset(txID, assetId);
                    }
                }

                if (message->m_MaxPrivacyMinAnonymitySet)
                {
                    params.SetParameter(TxParameterID::MaxPrivacyMinAnonimitySet, message->m_MaxPrivacyMinAnonymitySet);
                }

                const auto packed = params.Pack();
                for (const auto& p : packed)
                {
                    storage::setTxParameter(db, *params.GetTxID(), p.first, p.second, true);
                }
            }
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
                const string Identity = "Identity";
                const string Index = "Index";
                const string Label = "Label";
                const string CreationTime = "CreationTime";
                const string Duration = "Duration";
                const string TransactionId = "TransactionId";
                const string SubTransactionId = "SubTransactionId";
                const string ParameterId = "ParameterId";
                const string Value = "Value";
                const string Address = "Address";
            }
            
            bool ImportAddressesFromJson(IWalletDB& db, const json& obj, const string& nodeName)
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
                        if (!address.isOwn() || db.ValidateSbbsWalletID(address.m_walletID, address.m_OwnID))
                        {
                            //{ "SubIndex", 0 },
                            address.m_label = jsonAddress[Fields::Label].get<std::string>();
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
                                address.m_duration = WalletAddress::AddressExpirationAuto;
                            }
                            if (auto it = jsonAddress.find(Fields::Category); it != jsonAddress.end()) // for compatibility with older export
                            {
                                address.m_category = it->get<std::string>();
                            }
                            if (auto it = jsonAddress.find(Fields::Identity); it != jsonAddress.end())
                            {
                                bool isValid = false;
                                auto buf = from_hex(*it, &isValid);
                                if (isValid)
                                {
                                    address.m_Identity = Blob(buf);
                                }
                            }
                            if (auto it = jsonAddress.find(Fields::Address); it != jsonAddress.end())
                            {
                                address.m_Address = it->get<std::string>();
                            }
                            db.saveAddress(address);

                            LOG_INFO() << "The address [" << jsonAddress[Fields::WalletID] << "] has been successfully imported.";
                            continue;
                        }
                    }

                    LOG_INFO() << "The address [" << jsonAddress[Fields::WalletID] << "] has NOT been imported. Wrong address.";
                    return false;
                }
                return true;
            }

            bool ImportAddressesFromJson(IWalletDB& db, const json& obj)
            {
                return ImportAddressesFromJson(db, obj, Fields::OwnAddresses);
            }

            bool ImportContactsFromJson(IWalletDB& db, const json& obj)
            {
                return ImportAddressesFromJson(db, obj, Fields::Contacts);
            }

            bool ImportTransactionsFromJson(IWalletDB& db, const json& obj)
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

                    auto itype = paramsMap.find(TxParameterID::TransactionType);
                    if(itype == paramsMap.end())
                    {
                        LOG_ERROR() << "Transaction " << txPair.first << " was not imported. No txtype parameter";
                        return false;
                    }

                    TxType txtype = TxType::Simple;
                    if (!fromByteBuffer(itype->second.m_value, txtype))
                    {
                        LOG_ERROR() << "Transaction " << txPair.first << " was not imported. Failed to read txtype parameter";
                        return false;
                    }

                    WalletID wid;
                    if (auto idIt = paramsMap.find(TxParameterID::MyID); idIt == paramsMap.end() || !wid.FromBuf(idIt->second.m_value))
                    {
                        LOG_ERROR() << "Transaction " << txPair.first << " was not imported. Invalid myID parameter";
                        return false;
                    }

                    if(txtype == TxType::AssetConsume ||
                       txtype == TxType::AssetIssue ||
                       txtype == TxType::AssetReg ||
                       txtype == TxType::AssetUnreg ||
                       txtype == TxType::AssetInfo)
                    {
                        // Should be Zero for assets issue & consume
                        if (wid != Zero)
                        {
                            LOG_ERROR() << "Transaction " << txPair.first << " was not imported. Nonzero MyID for asset issue/consume";
                            return false;
                        }
                    } else
                    {
                        if (!wid.IsValid())
                        {
                            LOG_ERROR() << "Transaction " << txPair.first << " was not imported. Invalid myID parameter";
                            return false;
                        }

                        auto waddr = db.getAddress(wid);
                        if (waddr && (!waddr->isOwn() || !db.ValidateSbbsWalletID(wid, waddr->m_OwnID)))
                        {
                            LOG_ERROR() << "Transaction " << txPair.first << " was not imported. Invalid address parameter";
                            return false;
                        }

                        uint64_t myAddrId = 0;
                        auto addressIt = paramsMap.find(TxParameterID::MyAddressID);
                        if (addressIt != paramsMap.end() && (!fromByteBuffer(addressIt->second.m_value, myAddrId) ||
                            !db.ValidateSbbsWalletID(wid, myAddrId)))
                        {
                            LOG_ERROR() << "Transaction " << txPair.first << " was not imported. Invalid MyAddressID parameter";
                            return false;
                        }

                        if (!waddr && addressIt == paramsMap.end())
                        {
                            LOG_WARNING() << "Transaction " << txPair.first << ". Cannot check imported address";
                        }
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
                    addresses.emplace_back(
                        json
                        {
                            {Fields::Index, address.m_OwnID},
                            {"SubIndex", 0},
                            {Fields::WalletID, to_string(address.m_walletID)},
                            {Fields::Label, address.m_label},
                            {Fields::CreationTime, address.m_createTime},
                            {Fields::Duration, address.m_duration},
                            {Fields::Category, address.m_category},
                            {Fields::Address,  address.m_Address}
                        }
                    );
                    if (address.m_Identity != Zero)
                    {
                        addresses.back().push_back({ Fields::Identity, to_string(address.m_Identity) });
                    }
                }
                return addresses;
            }


            json ExportTransactionsToJson(const IWalletDB& db)
            {
                json txParams = json::array();
                map<TxID, map<SubTxID, map<TxParameterID, ByteBuffer>>> exportedParams;
                set<TxID> txIDs;
                for (const auto& p : db.getAllTxParameters())
                {
                    exportedParams[p.m_txID][(SubTxID)p.m_subTxID].emplace((TxParameterID)p.m_paramID, p.m_value);
                    txIDs.insert(p.m_txID);
                }

                array<TxParameterID, 5> mandatoryTxParams = { 
                        TxParameterID::TransactionType,
                        TxParameterID::CreateTime
                };

                for (const auto& tx : txIDs)
                {
                    const auto& params = exportedParams[tx][(int)kDefaultSubTxID];

                    if (params.size() == 1 && params.begin()->first == TxParameterID::TransactionType) // we do not export deleted transactions
                    {
                        continue;
                    }

                    bool canExport = true;
                    for (const auto& mp : mandatoryTxParams)
                    {
                        if (params.find(mp) == params.end())
                        {
                            LOG_WARNING() << "Transaction " << tx << " doesn't have mandatory parameters" << (int)mp << ". Skipping it";
                            canExport = false;
                            break;
                        }
                    }
                    if (!canExport)
                    {
                        continue;
                    }

                    for (const auto& subTx : exportedParams[tx])
                    {
                        for (const auto& p : subTx.second)
                        {
                            txParams.push_back(
                                json
                                {
                                    {Fields::TransactionId, tx},
                                    {Fields::SubTransactionId, subTx.first},
                                    {Fields::ParameterId, p.first},
                                    {Fields::Value, p.second}
                                }
                            );
                        }
                    }
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

        bool ImportDataFromJson(IWalletDB& db, const char* data, size_t size)
        {
            try
            {
                json obj = json::parse(data, data + size);
                return ImportAddressesFromJson(db, obj) 
                    && ImportContactsFromJson(db, obj)
                    && ImportTransactionsFromJson(db, obj);
            }
            catch (const nlohmann::detail::exception& e)
            {
                LOG_ERROR() << "json parse: " << e.what() << "\n" << std::string(data, data + (size > 1024 ? 1024 : size));
            }
            return false;
        }

        std::pair<std::string, std::string> getAssetNames(beam::Asset::ID assetID, const IWalletDB& wdb)
        {
            std::string uname, nthname;

            if (assetID)
            {
                uname = kAmountASSET;
                nthname = kAmountAGROTH;

                if (auto asset = wdb.findAsset(assetID))
                {
                    WalletAssetMeta meta(*asset);
                    if (meta.isStd())
                    {
                        uname = meta.GetUnitName();
                        nthname = meta.GetNthUnitName();
                    }
                }
            }

            return std::make_pair(uname, nthname);
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
            pc.m_AssetID = m_AssetID;
            return pc.IsValid(m_Receiver.m_Pk);
        }

        std::string PaymentInfo::to_string(const IWalletDB& wdb) const
        {
            const auto names = getAssetNames(m_AssetID, wdb);

            std::ostringstream s;
            s
                << "Sender:            " << std::to_string(m_Sender) << "\n"
                << "Receiver:          " << std::to_string(m_Receiver) << "\n"
                << "Amount:            " << PrintableAmount(m_Amount, false, names.first, names.second) << "\n"
                << "KernelID:          " << std::to_string(m_KernelID) << "\n";
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

        bool ShieldedPaymentInfo::IsValid() const
        {
            ShieldedTxo::Voucher voucher;
            voucher.m_SharedSecret = m_VoucherSharedSecret;
            voucher.m_Signature = m_VoucherSignature;
            voucher.m_Ticket = m_TxoTicket;

            if (!voucher.IsValid(m_Receiver))
            {
                return false;
            }

            return true;
        }

        std::string ShieldedPaymentInfo::to_string(const IWalletDB& wdb) const
        {
            const auto names = getAssetNames(m_AssetID, wdb);

            std::ostringstream s;
            s
                << "Sender:            " << std::to_string(m_Sender) << "\n"
                << "Receiver:          " << std::to_string(m_Receiver) << "\n"
                << "Amount:            " << PrintableAmount(m_Amount, false, names.first, names.second) << "\n"
                << "KernelID:          " << std::to_string(m_KernelID) << "\n";
            return s.str();
        }

        ShieldedPaymentInfo ShieldedPaymentInfo::FromByteBuffer(const ByteBuffer& data)
        {
            ShieldedPaymentInfo pi;
            if (!data.empty())
            {
                Deserializer der;
                der.reset(data);
                der& pi;
                if (der.bytes_left() > 0)
                {
                    throw std::runtime_error("Invalid data buffer");
                }
                pi.RestoreKernelID();
            }
            return pi;
        }

        void ShieldedPaymentInfo::RestoreKernelID()
        {
            // restore kernels
            TxKernelStd kernel;
            kernel.m_Fee = m_Fee;
            kernel.m_Height = m_Height;
            kernel.m_CanEmbed = false;
            kernel.m_Commitment = m_Commitment;

            auto nestedKernel = std::make_unique<TxKernelShieldedOutput>();
            nestedKernel->m_CanEmbed = true;
            nestedKernel->m_Txo.m_Ticket = m_TxoTicket;
            nestedKernel->UpdateMsg();

            ECC::Oracle oracle;
            oracle << nestedKernel->m_Msg;
            ShieldedTxo::Data::OutputParams outputParams;
            outputParams.m_Value = m_Amount;
            outputParams.m_AssetID = m_AssetID;
            outputParams.m_User.m_Sender = m_Sender;
            outputParams.m_User.m_pMessage[0] = m_pMessage[0];
            outputParams.m_User.m_pMessage[1] = m_pMessage[1];
            outputParams.Restore_kG(m_VoucherSharedSecret);
            outputParams.Generate(nestedKernel->m_Txo, m_VoucherSharedSecret, m_Height.m_Min, oracle, m_HideAssetAlways);

            nestedKernel->MsgToID();
            kernel.m_vNested.push_back(std::move(nestedKernel));

            kernel.UpdateID();
            m_KernelID = kernel.m_Internal.m_ID;
        }

        namespace
        {
            ByteBuffer ExportSimplePaymentProof(const IWalletDB& walletDB, const TxID& txID)
            {
                PaymentInfo pi;
                uint64_t nAddrOwnID = 0;

                bool bSuccess =
                    (
                        (
                            storage::getTxParameter(walletDB, txID, TxParameterID::PeerWalletIdentity, pi.m_Receiver.m_Pk) &&  // payment proiof using wallet ID
                            storage::getTxParameter(walletDB, txID, TxParameterID::MyWalletIdentity, pi.m_Sender.m_Pk)
                            ) ||
                        (
                            storage::getTxParameter(walletDB, txID, TxParameterID::PeerID, pi.m_Receiver) && // payment proof using SBBS address
                            storage::getTxParameter(walletDB, txID, TxParameterID::MyID, pi.m_Sender)
                            )
                        )
                    &&
                    storage::getTxParameter(walletDB, txID, TxParameterID::KernelID, pi.m_KernelID) &&
                    storage::getTxParameter(walletDB, txID, TxParameterID::Amount, pi.m_Amount) &&
                    storage::getTxParameter(walletDB, txID, TxParameterID::PaymentConfirmation, pi.m_Signature) &&
                    storage::getTxParameter(walletDB, txID, TxParameterID::MyAddressID, nAddrOwnID);

                // There might be old transactions without asset id
                if (!storage::getTxParameter(walletDB, txID, TxParameterID::AssetID, pi.m_AssetID))
                {
                    pi.m_AssetID = Asset::s_BeamID;
                    LOG_DEBUG() << "ExportPaymentProof, transaction " << txID << " is without assetId, defaulting to 0 (BEAM)";
                }

                if (bSuccess)
                {
                    std::cout << "Payment tx details:\n" << pi.to_string(walletDB) << "\n";
                    std::cout << "Sender address own ID: " << nAddrOwnID << std::endl;

                    Serializer ser;
                    ser& pi;

                    auto res = ser.buffer();
                    return ByteBuffer(res.first, res.first + res.second);
                }
                else
                {
                    LOG_WARNING() << "No payment confirmation for the specified transaction.";
                }

                return ByteBuffer();
            }

            ByteBuffer ExportShieldedPaymentProof(const IWalletDB& walletDB, const TxID& txID)
            {
                ShieldedPaymentInfo pi;
                TxKernel::Ptr rootKernel;
                ShieldedTxo::Voucher voucher;
                bool bSuccess =
                    storage::getTxParameter(walletDB, txID, TxParameterID::PeerWalletIdentity, pi.m_Receiver) &&
                    storage::getTxParameter(walletDB, txID, TxParameterID::MyWalletIdentity, pi.m_Sender) &&
                    storage::getTxParameter(walletDB, txID, TxParameterID::Voucher, voucher) &&
                    storage::getTxParameter(walletDB, txID, TxParameterID::Kernel, rootKernel) &&
                    storage::getTxParameter(walletDB, txID, TxParameterID::Amount, pi.m_Amount) &&
                    rootKernel->get_Subtype() == TxKernel::Subtype::Std &&
                    rootKernel->m_vNested.size() == 1 &&
                    rootKernel->m_vNested[0]->get_Subtype() == TxKernel::Subtype::ShieldedOutput;

                storage::getTxParameter(walletDB, txID, TxParameterID::AssetID, pi.m_AssetID);

                if (bSuccess)
                {
                    auto& stdKernel = rootKernel->CastTo_Std();

                    pi.m_Commitment = stdKernel.m_Commitment;
                    pi.m_Fee = stdKernel.m_Fee;
                    pi.m_Height = stdKernel.m_Height;
                    pi.m_Signature = stdKernel.m_Signature;

                    auto& nestedKernel = rootKernel->m_vNested[0]->CastTo_ShieldedOutput();
                    pi.m_TxoTicket = nestedKernel.m_Txo.m_Ticket;

                    pi.m_VoucherSharedSecret = voucher.m_SharedSecret;
                    pi.m_VoucherSignature = voucher.m_Signature;
                    {
                        ECC::Oracle oracle;
                        oracle << nestedKernel.m_Msg;
                        ShieldedTxo::Data::OutputParams outputParams;
                        outputParams.Recover(nestedKernel.m_Txo, voucher.m_SharedSecret, rootKernel->m_Height.m_Min, oracle);
                        pi.m_pMessage[0] = outputParams.m_User.m_pMessage[0];
                        pi.m_pMessage[1] = outputParams.m_User.m_pMessage[1];
                    }
                    pi.m_HideAssetAlways = ((pi.m_AssetID == Asset::s_InvalidID) && nestedKernel.m_Txo.m_pAsset);
                    pi.RestoreKernelID();
                    std::cout << "Payment tx details:\n" << pi.to_string(walletDB) << std::endl;

                    Serializer ser;
                    ser& pi;

                    auto res = ser.buffer();
                    return ByteBuffer(res.first, res.first + res.second);
                }
                else
                {
                    LOG_WARNING() << "No payment confirmation for the specified transaction.";
                }

                return ByteBuffer();
            }

            template<typename T>
            bool VerifyPaymentProofImpl(const ByteBuffer& data, const IWalletDB& wdb)
            {
                try
                {
                    T pi = T::FromByteBuffer(data);

                    if (!pi.IsValid())
                    {
                        return false;
                    }

                    std::cout << "Payment tx details:\n" << pi.to_string(wdb) << "Verified." << std::endl;

                    return true;
                }
                catch (...)
                {

                }
                return false;
            }
        }

        ByteBuffer ExportPaymentProof(const IWalletDB& walletDB, const TxID& txID)
        {
            TxType txType = TxType::Simple;
            if (!storage::getTxParameter(walletDB, txID, TxParameterID::TransactionType, txType))
            {
                LOG_ERROR() << "There is no transaction with given ID.";
                return {};
            }
            switch (txType)
            {
            case TxType::Simple:
                return ExportSimplePaymentProof(walletDB, txID);
            case TxType::PushTransaction:
                return ExportShieldedPaymentProof(walletDB, txID);
            default:
                LOG_WARNING() << "Cannot provide payment proof for this transaction type: " << (int)txType;
            }
            
            return ByteBuffer();
        }

        bool VerifyPaymentProof(const ByteBuffer& data, const IWalletDB& wdb)
        {
            return VerifyPaymentProofImpl<PaymentInfo>(data, wdb)
                || VerifyPaymentProofImpl<ShieldedPaymentInfo>(data, wdb);
        }


        void SaveVouchers(IWalletDB& walletDB, const ShieldedVoucherList& vouchers, const WalletID& walletID)
        {
            try
            {
                for (const auto& v : vouchers)
                {
                    walletDB.saveVoucher(v, walletID, true);
                }
            }
            catch (const DatabaseException&)
            {
                // probably, we are trying to insert an existing voucher, ingnore
            }
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

        bool isMyAddress(
            const std::vector<WalletAddress>& myAddresses, const WalletID& wid)
        {
            auto myAddrIt = std::find_if(
                myAddresses.begin(),
                myAddresses.end(),
                [&wid] (const WalletAddress& addr)
                {
                    return wid == addr.m_walletID;
                });
            return myAddrIt != myAddresses.end();
        }
    }

    ////////////////////////
    // WalletAddress
    WalletAddress::WalletAddress(const std::string& label, const std::string& category)
        : m_walletID(Zero)
        , m_label(label)
        , m_category(category)
        , m_createTime(0)
        , m_duration(AddressExpiration24h)
        , m_OwnID(0)
        , m_Identity(Zero)
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

    bool WalletAddress::isOwn() const
    {
        return m_OwnID != 0;
    }

    bool WalletAddress::isPermanent() const
    {
        return m_duration == AddressExpirationNever;
    }

    Timestamp WalletAddress::getCreateTime() const
    {
        return m_createTime;
    }

    Timestamp WalletAddress::getExpirationTime() const
    {
        if (isPermanent())
        {
            return Timestamp(-1);
        }
        return m_createTime + m_duration;
    }

    void WalletAddress::setLabel(const std::string& label)
    {
        m_label = label;
    }

    void WalletAddress::setCategory(const std::string& category)
    {
        m_category = category;
    }

    void WalletAddress::setExpirationTime(beam::Timestamp expirationTime)
    {
        if (m_createTime <= expirationTime)
        {
            m_duration = expirationTime - getCreateTime();
        }
        else
        {
            assert(!"invalid expiration time");
            setExpirationStatus(ExpirationStatus::Expired);
        }
    }

    void WalletAddress::setExpirationStatus(WalletAddress::ExpirationStatus status)
    {
        switch (status)
        {
        case ExpirationStatus::Expired:
            {
                assert(m_createTime < getTimestamp() - 1);
                m_duration = getTimestamp() - m_createTime - 1;
                break;
            }
        case ExpirationStatus::Auto:
            {
                // set expiration date since current timestamp
                auto delta = getTimestamp() - m_createTime;
                m_duration = delta + WalletAddress::AddressExpirationAuto;
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

    uint32_t ShieldedCoin::get_WndIndex(uint32_t N) const
    {
        assert(N);

        uint32_t nIdx;
        m_CoinID.m_Key.m_kSerG.m_Value.ExportWord<0>(nIdx); // pseudo-random

        nIdx %= N; // pseudo-random, how many elements should follow this in the window

        if (nIdx > m_TxoID)
            nIdx = static_cast<uint32_t>(m_TxoID);

        return nIdx;
    }

    void ShieldedCoin::UnlinkStatus::Init(const ShieldedCoin& sc, TxoID nShieldedOuts)
    {
        if (kTxoInvalidID == sc.m_TxoID)
        {
            m_Progress = 0;
            m_WndReserve0 = 0;
            m_WndReserve1 = 0;
        }
        else
        {
            const Rules& r = Rules::get();
            const uint32_t N = std::max(r.Shielded.m_ProofMax.get_N(), 1U);

            uint32_t nRemaining = N - sc.get_WndIndex(N) - 1;

            std::setmax(nShieldedOuts, sc.m_TxoID + 1);
            nShieldedOuts -= sc.m_TxoID; // to relative
            assert(nShieldedOuts);

            if (nShieldedOuts <= nRemaining)
            {
                assert(nRemaining);

                uint32_t n = static_cast<uint32_t>(nShieldedOuts) - 1;
                assert(n < nRemaining);

                m_Progress = 1 + 99U * n / nRemaining;
            }
            else
                m_Progress = 100;

            m_WndReserve0 = get_Reserve(nRemaining + 1, nShieldedOuts);
            m_WndReserve1 = get_Reserve(N, nShieldedOuts);
        }
    }

    template <typename TDst, typename TSrc>
    TDst CastSaturated(const TSrc& x)
    {
        return
            (x > std::numeric_limits<TDst>::max()) ? std::numeric_limits<TDst>::max() :
            (x < std::numeric_limits<TDst>::min()) ? std::numeric_limits<TDst>::min() :
            static_cast<TDst>(x);
    }

    int32_t ShieldedCoin::get_Reserve(uint32_t nEndRel, TxoID nShieldedOutsRel)
    {
        int64_t n = nEndRel;
        n += Rules::get().Shielded.MaxWindowBacklog;
        n -= Rules::get().Shielded.MaxIns; // safety thershold.

        static_assert(sizeof(n) == sizeof(nShieldedOutsRel)); // both should be 64bit
        n -= nShieldedOutsRel;

        return CastSaturated<int32_t>(n);
    }

    bool ShieldedCoin::UnlinkStatus::IsLargeSpendWindowLost() const
    {
        return m_WndReserve1 < 0;
    }

    int ShieldedCoin::UnlinkStatus::get_SpendPriority() const
    {
        if (IsLargeSpendWindowLost())
            return 0;

        const int32_t nWndThresholdHi = 100; // Urgently spend if window will close in less than this
        if (m_WndReserve1 < nWndThresholdHi)
            return 2;

        if (100 == m_Progress)
            return 1;

        return -1;
    }

    void ShieldedCoin::Sort(std::vector<ShieldedCoin::WithStatus>& v)
    {
        struct MyCmp
        {
            bool operator ()(const WithStatus& a, const WithStatus& b) const
            {
                // return if a should be spend before b
                int na = a.second.get_SpendPriority();
                int nb = b.second.get_SpendPriority();
                if (na < nb)
                    return false;
                if (na > nb)
                    return true;

                if (-1 == na)
                {
                    // if both are washing - spend the one that is cleaner
                    if (a.second.m_WndReserve0 < b.second.m_WndReserve0)
                        return true;
                    if (a.second.m_WndReserve0 > b.second.m_WndReserve0)
                        return false;
                }

                // not really important, but for steady sort let's resolve ambiguity by coin unique ID
                return a.first.m_TxoID < b.first.m_TxoID;
            }
        } mcmp;

        std::sort(v.begin(), v.end(), mcmp);
    }

    namespace
    {
        TxParameters GenerateCommonAddressPart(Amount amount, Asset::ID assetId, const std::string& clientVersion)
        {
            TxParameters params;

            if (amount > 0)
            {
                params.SetParameter(TxParameterID::Amount, amount);
            }

            if (assetId != Asset::s_InvalidID)
            {
                params.SetParameter(TxParameterID::AssetID, assetId);
            }

            if (!clientVersion.empty())
            {
                params.SetParameter(TxParameterID::ClientVersion, clientVersion);
            }

            AppendLibraryVersion(params);
            return params;
        }
    }

    std::string GenerateOfflineToken(const WalletAddress& address, Amount amount, Asset::ID assetId, const ShieldedVoucherList& vouchers, const std::string& clientVersion)
    {
        TxParameters params = GenerateCommonAddressPart(amount, assetId, clientVersion);

        params.SetParameter(TxParameterID::TransactionType, beam::wallet::TxType::PushTransaction);
        params.SetParameter(TxParameterID::ShieldedVoucherList, vouchers);
        params.SetParameter(TxParameterID::PeerID,              address.m_walletID);
        params.SetParameter(TxParameterID::PeerWalletIdentity,  address.m_Identity);
        params.SetParameter(TxParameterID::PeerOwnID,           address.m_OwnID);
        params.SetParameter(TxParameterID::IsPermanentPeerID,   address.isPermanent());

        return std::to_string(params);
    }

    std::string GenerateMaxPrivacyToken(const WalletAddress& address, Amount amount, Asset::ID assetId, const ShieldedTxo::Voucher& voucher, const std::string& clientVersion)
    {
        TxParameters params = GenerateCommonAddressPart(amount, assetId, clientVersion);

        params.SetParameter(TxParameterID::TransactionType, beam::wallet::TxType::PushTransaction);
        params.SetParameter(TxParameterID::PeerWalletIdentity, address.m_Identity);
        params.SetParameter(TxParameterID::PeerOwnID, address.m_OwnID);
        params.SetParameter(TxParameterID::Voucher, voucher);

        return std::to_string(params);
    }

    std::string GeneratePublicToken(const IWalletDB& walletDB, const std::string& clientVersion)
    {
        TxParameters params = GenerateCommonAddressPart(0, Asset::s_InvalidID, clientVersion);

        params.SetParameter(TxParameterID::TransactionType, beam::wallet::TxType::PushTransaction);
        params.SetParameter(TxParameterID::PublicAddreessGen, GeneratePublicAddress(*walletDB.get_OwnerKdf(), 0));
        AppendLibraryVersion(params);

        return std::to_string(params);
    }

    std::string  GenerateRegularOldToken(const WalletAddress& address)
    {
        return std::to_string(address.m_walletID);
    }

    std::string  GenerateRegularNewToken(const WalletAddress& address, Amount amount, Asset::ID assetId, const std::string& clientVersion)
    {
        TxParameters params = GenerateCommonAddressPart(amount, assetId, clientVersion);

        params.SetParameter(TxParameterID::PeerID, address.m_walletID);
        params.SetParameter(TxParameterID::PeerWalletIdentity, address.m_Identity);
        params.SetParameter(TxParameterID::IsPermanentPeerID, address.isPermanent());
        params.SetParameter(TxParameterID::TransactionType, TxType::Simple);

        return std::to_string(params);
    }

    std::string  GenerateToken (TokenType type, const WalletAddress& address, IWalletDB::Ptr walletDB, boost::optional<uint32_t> offlineCount)
    {
        if (type == TokenType::Public)
        {
            auto token = GeneratePublicToken(*walletDB, "");
            LOG_INFO() << "Generated public offline address: " << token;
            return token;
        }

        switch (type)
        {
        case TokenType::RegularOldStyle:
            {
                auto token = GenerateRegularOldToken(address);
                LOG_INFO() << "Generated regular old style address: " << token;
                return token;
            }

        case TokenType::RegularNewStyle:
             {
                auto token = GenerateRegularNewToken(address, 0, 0, "");
                LOG_INFO() << "Generated regular new style address:" << token;
                return token;
            }

        case TokenType::Offline:
            {
                auto vouchers = GenerateVoucherList(walletDB->get_KeyKeeper(), address.m_OwnID, offlineCount ? *offlineCount : 10);
                auto token = GenerateOfflineToken(address, 0, 0, vouchers, "");
                LOG_INFO() << "Generated offline address: " << token << ", vouchers count " << *offlineCount;
                return token;
            }

        case TokenType::MaxPrivacy:
            {
                auto vouchers = GenerateVoucherList(walletDB->get_KeyKeeper(), address.m_OwnID, 1);
                auto token = GenerateMaxPrivacyToken(address, 0, 0, vouchers[0], "");
                LOG_INFO() << "Generated max privacy address: " << token;
                return token;
            }

        default:
            throw std::runtime_error("Unexpected address type");
        }
    }

    TokenType GetTokenType(const std::string& token)
    {
        const auto addrType = GetAddressType(token);
        switch (addrType)
        {
            case TxAddressType::Offline: return TokenType::Offline;
            case TxAddressType::MaxPrivacy: return TokenType::MaxPrivacy;
            case TxAddressType::PublicOffline: return TokenType::Public;
            case TxAddressType::Regular:
            {
                if(auto params = ParseParameters(token))
                {
                    WalletID wid;
                    if(!params->GetParameter(TxParameterID::PeerID, wid))
                    {
                        assert(false);
                        return TokenType::RegularOldStyle;
                    }

                    if (std::to_string(wid) == token)
                    {
                        return TokenType::RegularOldStyle;
                    }

                    return TokenType::RegularNewStyle;
                }
                else
                {
                    assert(false);
                    return TokenType::Unknown;
                }
            }
            default: return TokenType::Unknown;
        }
    }
}
