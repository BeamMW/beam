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
#define AND " AND "


#define ENUM_STORAGE_ID(each, sep, obj) \
    each(Type,           ID.m_Type,     INTEGER NOT NULL, obj) sep \
    each(SubKey,         ID.m_iChild,   INTEGER NOT NULL, obj) sep \
    each(Number,         ID.m_Idx,      INTEGER NOT NULL, obj)

#define ENUM_STORAGE_FIELDS(each, sep, obj) \
    each(amount,         ID.m_Value,    INTEGER NOT NULL, obj) sep \
    each(status,         status,        INTEGER NOT NULL, obj) sep \
    each(maturity,       maturity,      INTEGER NOT NULL, obj) sep \
    each(createHeight,   createHeight,  INTEGER NOT NULL, obj) sep \
    each(confirmHeight,  confirmHeight, INTEGER, obj) sep \
    each(lockedHeight,   lockedHeight,  BLOB, obj) sep \
    each(createTxId,     createTxId,    BLOB, obj) sep \
    each(spentTxId,      spentTxId,     BLOB, obj)

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
#define PEERS_NAME "peers"
#define ADDRESSES_NAME "addresses"
#define TX_PARAMS_NAME "txparams"

#define ENUM_VARIABLES_FIELDS(each, sep, obj) \
    each(name,  name,  TEXT UNIQUE, obj) sep \
    each(value, value, BLOB, obj)

#define VARIABLES_FIELDS ENUM_VARIABLES_FIELDS(LIST, COMMA, )

#define ENUM_HISTORY_FIELDS(each, sep, obj) \
    each(txId,       txId,       BLOB NOT NULL PRIMARY KEY, obj) sep \
    each(amount,     amount,     INTEGER NOT NULL, obj) sep \
    each(fee,        fee,        INTEGER NOT NULL, obj) sep \
    each(minHeight,  minHeight,  INTEGER NOT NULL, obj) sep \
    each(peerId,     peerId,     BLOB NOT NULL, obj) sep \
    each(myId,       myId,       BLOB NOT NULL, obj) sep \
    each(message,    message,    BLOB, obj) sep \
    each(createTime, createTime, INTEGER NOT NULL, obj) sep \
    each(modifyTime, modifyTime, INTEGER, obj) sep \
    each(sender,     sender,     INTEGER NOT NULL, obj) sep \
    each(status,     status,     INTEGER NOT NULL, obj) sep \
    each(fsmState,   fsmState,   BLOB, obj) sep \
    each(change,     change,     INTEGER NOT NULL, obj)

#define HISTORY_FIELDS ENUM_HISTORY_FIELDS(LIST, COMMA, )

#define ENUM_PEER_FIELDS(each, sep, obj) \
    each(walletID,    walletID,    BLOB NOT NULL PRIMARY KEY, obj) sep \
    each(address,     address,     TEXT NOT NULL, obj) sep \
    each(label,       label,       TEXT NOT NULL , obj)

#define PEER_FIELDS ENUM_PEER_FIELDS(LIST, COMMA, )


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
    each(paramID,        paramID,        INTEGER NOT NULL , obj) sep \
    each(value,          value,          BLOB, obj)

#define TX_PARAMS_FIELDS ENUM_TX_PARAMS_FIELDS(LIST, COMMA, )

#define TblStates            "States"
#define TblStates_Height    "Height"
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

namespace beam
{
    using namespace std;

    namespace
    {
        static const char g_szBbsTime[] = "Bbs-Channel-Upd";
        static const char g_szBbsChannel[] = "Bbs-Channel";

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
					size_t m_iHead;
					size_t m_iTail;
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
					return (i >= s_Factor) ? s_Factor : static_cast<uint32_t>(i);
				}

				void Append(Slot& rDst, Amount v, size_t i0)
				{
					m_vLinks.emplace_back();
					m_vLinks.back().m_iElement = i0;
					m_vLinks.back().m_iNext = rDst.m_iTail;

					rDst.m_iTail = m_vLinks.size();
					if (!rDst.m_iHead)
						rDst.m_iHead = rDst.m_iTail;

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

						for (size_t iLink = part.m_pSlots[iSlot].m_iHead; iLink; )
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

					for (size_t iLink = r1.m_iHead; iLink; )
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

        struct CoinSelector2
        {
            struct CoinEx
            {
                Coin m_coin;
                Amount m_lowerTotal;
            };

            using Result = pair<Amount, vector<Coin>>;
            CoinSelector2(const vector<Coin>& coins)
                :/* m_coins(coins.size())
                , */m_amount(0)
                , m_lowerBorder(0)
            {
                Amount sum = 0;
                /*for (const auto& coin : coins)
                {
                    sum += coin.m_ID.m_Value;
                    m_coins.emplace_back({coin, });
                }*/
                for (auto idx = coins.rbegin(); idx != coins.rend(); idx++)
                {
                    sum += idx->m_ID.m_Value;
                    m_coins.push_back({ *idx, sum});
                }

                std::reverse(m_coins.begin(), m_coins.end());
            }

            Result select(Amount amount)
            {
                m_amount = amount;
                m_result.first = 0;
                m_result.second.clear();

                FindLowerBorder();
                GenerateCombinations();
                FindBestResult();
                SelectCoins();

                LOG_INFO() << "m_result.first = " << m_result.first << " size = " << m_result.second.size();
                return m_result;
            }

        private:

            void FindLowerBorder()
            {
                Amount sum = 0;

                //for (const auto& coin : m_coins)
                for (auto idx = m_coins.rbegin(); idx != m_coins.rend(); ++idx)
                {
                    if (sum + idx->m_coin.m_ID.m_Value >= m_amount)
                    {
                        break;
                    }
                    m_lowerBorder = idx->m_coin.m_ID.m_Value;
                    sum += m_lowerBorder;
                }

                sum = 0;

                for (const auto& coin : m_coins)
                {
                    sum += coin.m_coin.m_ID.m_Value;
                }

                LOG_INFO() << "amount = " << m_amount << " all sum = " << sum;
            }

            void GenerateCombinations()
            {
                int i = 0;
                for (auto coin = m_coins.begin(); coin != m_coins.end(); ++coin, ++i)
                {
                    if (coin->m_coin.m_ID.m_Value > m_amount)
                    {
                        m_Combinations[coin->m_coin.m_ID.m_Value] = coin->m_coin.m_ID.m_Value;
                        continue;
                    }

                    if (coin->m_coin.m_ID.m_Value == m_amount)
                    {
                        m_Combinations[coin->m_coin.m_ID.m_Value] = coin->m_coin.m_ID.m_Value;
                        break;
                    }

                    vector<Amount> newCombinations;

                    newCombinations.reserve(m_Combinations.size() + 100);
                    if (coin->m_coin.m_ID.m_Value >= m_lowerBorder)
                    {
                        auto it = m_Combinations.find(coin->m_coin.m_ID.m_Value);
                        if (it == m_Combinations.end())
                        {
                            newCombinations.push_back(coin->m_coin.m_ID.m_Value);
                        }
                    }

                    for (const auto& sum : m_Combinations)
                    {
                        if (sum.first < m_amount && m_amount <= sum.first + coin->m_lowerTotal)
                            newCombinations.push_back(sum.first + coin->m_coin.m_ID.m_Value);
                    }

                    for (const auto& sum : newCombinations)
                    {
                        auto it = m_Combinations.find(sum);
                        if (it == m_Combinations.end())
                        {
                            m_Combinations[sum] = coin->m_coin.m_ID.m_Value;
                        }
                    }

                    if (m_Combinations.find(m_amount) != m_Combinations.end())
                        break;
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
                    auto it = find_if(m_coins.begin(), m_coins.end(), [amount = p.first](const CoinEx& c)
                    {
                        return c.m_coin.m_ID.m_Value == amount;
                    });

                    for (Amount i = 0; i < p.second; ++i, ++it)
                    {
                        m_result.second.push_back(it->m_coin);
                    }
                }
            }

            vector<CoinEx> m_coins;
            Result m_result;
            Amount m_amount;
            map<Amount, Amount> m_Combinations;
            map<Amount, Amount> m_intermediateResult;
            Amount m_lowerBorder;
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

                Amount coinAmount = m_it->m_ID.m_Value;
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

            void get(int col, uint32_t& val)
            {
                val = sqlite3_column_int(_stm, col);
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
        const int DbVersion = 8;
    }

    Coin::Coin(Amount amount, Status status, Height maturity, Key::Type keyType, Height confirmHeight, Height lockedHeight)
        : m_status{ status }
        , m_createHeight(0)
        , m_maturity{ maturity }
        , m_confirmHeight{ confirmHeight }
        , m_lockedHeight{ lockedHeight }
    {
        ZeroObject(m_ID);
        m_ID.m_Value = amount;
        m_ID.m_Type = keyType;

        assert(isValid());
    }

    Coin::Coin()
        : Coin(0, Coin::Available, MaxHeight, Key::Type::Regular, MaxHeight)
    {
        assert(isValid());
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

    bool Coin::isValid() const
    {
        return m_maturity <= m_lockedHeight;
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
                                  "CREATE UNIQUE INDEX CoinIndex ON " STORAGE_NAME "(" ENUM_STORAGE_ID(LIST, COMMA, )  ");"
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
                    "[" TblStates_Height    "] INTEGER NOT NULL PRIMARY KEY,"
                    "[" TblStates_Hdr        "] BLOB NOT NULL)";
                int ret = sqlite3_exec(walletDB->_db, req, nullptr, nullptr, nullptr);
                throwIfError(ret, walletDB->_db);
            }

            {
                wallet::setVar(walletDB, WalletSeed, secretKey.V);
                wallet::setVar(walletDB, Version, DbVersion);
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
                    if (!wallet::getVar(walletDB, Version, version) || version > DbVersion)
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

                ECC::NoLeak<ECC::Hash::Value> seed;
                if (!wallet::getVar(walletDB, WalletSeed, seed.V))
                {
                    assert(false && "there is no seed for walletDB");
                    //pKdf->m_Secret.V = Zero;
                    return Ptr();
                }

                ECC::HKdf::Create(walletDB->m_pKdf, seed.V);

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
        ECC::HKdf::Create(m_pKdf, secretKey.V);
    }

    WalletDB::~WalletDB()
    {
        if (_db)
        {
            sqlite3_close_v2(_db);
            _db = nullptr;
        }
    }

    Key::IKdf::Ptr WalletDB::get_MasterKdf() const
    {
        return m_pKdf;
    }

    Key::IKdf::Ptr IWalletDB::get_ChildKdf(Key::Index iKdf) const
    {
        Key::IKdf::Ptr pMaster = get_MasterKdf();
        if (!iKdf)
            return pMaster; // by convention 0 is not a childd

        Key::IKdf::Ptr pRet;
        ECC::HKdf::CreateChild(pRet, *pMaster, iKdf);
        return pRet;
    }

	void IWalletDB::calcCommitment(ECC::Scalar::Native& sk, ECC::Point& comm, const Coin::ID& cid)
	{
		SwitchCommitment::Create(sk, comm, *get_ChildKdf(cid.m_iChild), cid);
	}

    vector<Coin> WalletDB::selectCoins(const Amount& amount, bool lock)
    {
        vector<Coin> coins, coinsSel;
        Block::SystemState::ID stateID = {};
        getSystemStateID(stateID);

		{
			sqlite::Statement stm(_db, "SELECT " STORAGE_FIELDS " FROM " STORAGE_NAME " WHERE status=?1 AND maturity<=?2 ORDER BY amount ASC");
			stm.bind(1, Coin::Available);
			stm.bind(2, stateID.m_Height);

			while (stm.step())
			{
				auto& coin = coins.emplace_back();
				int colIdx = 0;
				ENUM_ALL_STORAGE_FIELDS(STM_GET_LIST, NOSEP, coin);

				if (coin.m_ID.m_Value >= amount)
					break;
			}
		}

		CoinSelector3 csel(coins);
		CoinSelector3::Result res = csel.Select(amount);

		if (res.first >= amount)
		{
			coinsSel.reserve(res.second.size());

			for (size_t j = 0; j < res.second.size(); j++)
				coinsSel.push_back(std::move(coins[res.second[j]]));

			if (lock)
			{
				sqlite::Transaction trans(_db);

				for (auto& coin : coinsSel)
				{
					coin.m_status = Coin::Outgoing;
					const char* req = "UPDATE " STORAGE_NAME " SET status=?, lockedHeight=?" STORAGE_WHERE_ID;
					sqlite::Statement stm(_db, req);

					int colIdx = 0;
					stm.bind(++colIdx, coin.m_status);
					stm.bind(++colIdx, stateID.m_Height);
					STORAGE_BIND_ID(coin)

					stm.step();
				}

				trans.commit();

				notifyCoinsChanged();
			}
		}


		return coinsSel;
    }

    std::vector<Coin> WalletDB::getCoinsCreatedByTx(const TxID& txId)
    {
        // select all coins for TxID
        sqlite::Statement stm(_db, "SELECT " STORAGE_FIELDS " FROM " STORAGE_NAME " WHERE createTxID=?1 ORDER BY amount DESC;");
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

    void WalletDB::store(Coin& coin)
    {
        sqlite::Transaction trans(_db);

        coin.m_ID.m_Idx = AllocateKidRange(1);
        storeImpl(coin);

        trans.commit();
        notifyCoinsChanged();
    }

    void WalletDB::store(std::vector<Coin>& coins)
    {
        if (coins.empty())
            return;

        sqlite::Transaction trans(_db);

        uint64_t nKeyIndex = AllocateKidRange(coins.size());

        for (auto& coin : coins)
        {
            coin.m_ID.m_Idx = nKeyIndex++;
            storeImpl(coin);
        }

        trans.commit();
        notifyCoinsChanged();
    }

    void WalletDB::save(const Coin& coin)
    {
        storeImpl(coin);
        notifyCoinsChanged();
    }

    void WalletDB::save(const vector<Coin>& coins)
    {
        if (coins.empty())
            return;

        sqlite::Transaction trans(_db);

        for (auto& coin : coins)
            storeImpl(coin);

        trans.commit();
        notifyCoinsChanged();
    }

    void WalletDB::storeImpl(const Coin& coin)
    {
        const char* req = "INSERT OR REPLACE INTO " STORAGE_NAME " (" ENUM_ALL_STORAGE_FIELDS(LIST, COMMA, ) ") VALUES(" ENUM_ALL_STORAGE_FIELDS(BIND_LIST, COMMA, ) ");";
        sqlite::Statement stm(_db, req);

        int colIdx = 0;
        ENUM_ALL_STORAGE_FIELDS(STM_BIND_LIST, NOSEP, coin);

        stm.step();
    }

    uint64_t WalletDB::AllocateKidRange(uint64_t nCount)
    {
        // a bit akward, but ok
        static const char szName[] = "LastKid";

        uint64_t nLast;
        uintBigFor<uint64_t>::Type var;
        auto thisPtr = shared_from_this();

        if (wallet::getVar(thisPtr, szName, var))
            var.Export(nLast);
        else
        {
            nLast = getTimestamp(); // by default initialize by current time X1M (1sec resolution) to prevent collisions after reinitialization. Should be ok if creating less than 1M keys / sec average
            nLast *= 1000000;
        }

        var = nLast + nCount;
        wallet::setVar(thisPtr, szName, var);

        return nLast;
    }

    void WalletDB::remove(const vector<Coin::ID>& coins)
    {
        if (coins.size())
        {
            sqlite::Transaction trans(_db);

            for (const auto& cid : coins)
                removeImpl(cid);

            trans.commit();
            notifyCoinsChanged();
        }
    }

    void WalletDB::removeImpl(const Coin::ID& cid)
    {
        const char* req = "DELETE FROM " STORAGE_NAME STORAGE_WHERE_ID;
        sqlite::Statement stm(_db, req);

        struct DummyWrapper {
            Coin::ID m_ID;
        };

        static_assert(sizeof(DummyWrapper) == sizeof(cid), "");
        const DummyWrapper& wrp = reinterpret_cast<const DummyWrapper&>(cid);

        int colIdx = 0;
        STORAGE_BIND_ID(wrp)

        stm.step();
    }

    void WalletDB::remove(const Coin::ID& cid)
    {
        removeImpl(cid);
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

    bool WalletDB::find(Coin& coin)
    {
        const char* req = "SELECT " ENUM_STORAGE_FIELDS(LIST, COMMA, ) " FROM " STORAGE_NAME STORAGE_WHERE_ID;
        sqlite::Statement stm(_db, req);

        int colIdx = 0;
        STORAGE_BIND_ID(coin)

        if (!stm.step())
            return false;

        colIdx = 0;
        ENUM_STORAGE_FIELDS(STM_GET_LIST, NOSEP, coin);

        return true;
    }

    void WalletDB::maturingCoins()
    {
        sqlite::Transaction trans(_db);

        {
            const char* req = "UPDATE " STORAGE_NAME " SET status=?3 WHERE status=?1 AND maturity <= ?2;";
            sqlite::Statement stm(_db, req);

            stm.bind(1, Coin::Maturing);
            stm.bind(2, getCurrentHeight());
            stm.bind(3, Coin::Available);

            stm.step();
        }

        trans.commit();
        notifyCoinsChanged();
    }

    void WalletDB::visit(function<bool(const Coin& coin)> func)
    {
        const char* req = "SELECT " STORAGE_FIELDS " FROM " STORAGE_NAME " ORDER BY " ENUM_STORAGE_ID(LIST, COMMA, ) ";";
        sqlite::Statement stm(_db, req);

        while (stm.step())
        {
            Coin coin;

            int colIdx = 0;
            ENUM_ALL_STORAGE_FIELDS(STM_GET_LIST, NOSEP, coin);

            if (!func(coin))
                break;
        }
    }

    void WalletDB::setVarRaw(const char* name, const void* data, size_t size)
    {
        const char* req = "INSERT or REPLACE INTO " VARIABLES_NAME " (" VARIABLES_FIELDS ") VALUES(?1, ?2);";

        sqlite::Statement stm(_db, req);

        stm.bind(1, name);
        stm.bind(2, data, size);

        stm.step();
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
        
        if (wallet::getVar(shared_from_this(), LastUpdateTimeName, timestamp))
        {
            return timestamp;
        }
        return 0;
    }

    void WalletDB::setSystemStateID(const Block::SystemState::ID& stateID)
    {
        auto thisPtr = shared_from_this();
        wallet::setVar(thisPtr, SystemStateIDName, stateID);
        wallet::setVar(thisPtr, LastUpdateTimeName, getTimestamp());
        notifySystemStateChanged();
        // update coins
        maturingCoins();
    }

    bool WalletDB::getSystemStateID(Block::SystemState::ID& stateID) const
    {
        return wallet::getVar(shared_from_this(), SystemStateIDName, stateID);
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
        sqlite::Transaction trans(_db);

        {
            const char* req = "UPDATE " STORAGE_NAME " SET status=?1, confirmHeight=?2, lockedHeight=?2 WHERE confirmHeight > ?3 ;";
            sqlite::Statement stm(_db, req);
            stm.bind(1, Coin::Unavailable);
            stm.bind(2, MaxHeight);
            stm.bind(3, minHeight);
            stm.step();
        }

        {
            const char* req = "UPDATE " STORAGE_NAME " SET status=?1, lockedHeight=?2 WHERE lockedHeight > ?3 AND confirmHeight <= ?3 ;";
            sqlite::Statement stm(_db, req);
            stm.bind(1, Coin::Available);
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
        const char* req = "SELECT * FROM " TX_PARAMS_NAME " WHERE txID=?1 LIMIT 1;";
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

        wallet::setTxParameter(thisPtr, p.m_txId, wallet::TxParameterID::Amount, p.m_amount, false);
        wallet::setTxParameter(thisPtr, p.m_txId, wallet::TxParameterID::Fee, p.m_fee, false);
        wallet::setTxParameter(thisPtr, p.m_txId, wallet::TxParameterID::Change, p.m_change, false);
        wallet::setTxParameter(thisPtr, p.m_txId, wallet::TxParameterID::MinHeight, p.m_minHeight, false);
        wallet::setTxParameter(thisPtr, p.m_txId, wallet::TxParameterID::PeerID, p.m_peerId, false);
        wallet::setTxParameter(thisPtr, p.m_txId, wallet::TxParameterID::MyID, p.m_myId, false);
        wallet::setTxParameter(thisPtr, p.m_txId, wallet::TxParameterID::Message, p.m_message, false);
        wallet::setTxParameter(thisPtr, p.m_txId, wallet::TxParameterID::CreateTime, p.m_createTime, false);
        wallet::setTxParameter(thisPtr, p.m_txId, wallet::TxParameterID::ModifyTime, p.m_modifyTime, false);
        wallet::setTxParameter(thisPtr, p.m_txId, wallet::TxParameterID::IsSender, p.m_sender, false);
        wallet::setTxParameter(thisPtr, p.m_txId, wallet::TxParameterID::Status, p.m_status, false);

        trans.commit();
        // notify only when full TX saved
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
            stm.bind(2, Coin::Outgoing);
            stm.bind(3, Coin::Available);
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
            int colIdx = 0;
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
        int colIdx = 0;
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
            int colIdx = 0;
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
        const char* req = "SELECT * FROM " ADDRESSES_NAME " ORDER BY createTime DESC;";

        sqlite::Statement stm(_db, req);

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
                int colIdx = 0;
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
            int colIdx = 0;
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

    Timestamp WalletDB::GetLastChannel(BbsChannel& ch)
    {
        Timestamp t;
        auto thisPtr = shared_from_this();
        bool b =
            wallet::getVar(thisPtr, g_szBbsTime, t) &&
            wallet::getVar(thisPtr, g_szBbsChannel, ch);

            return b ? t : 0;
    }

    void WalletDB::SetLastChannel(BbsChannel ch)
    {
        auto thisPtr = shared_from_this();

        wallet::setVar(thisPtr, g_szBbsChannel, ch);
        wallet::setVar(thisPtr, g_szBbsTime, getTimestamp());
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

    bool WalletDB::setTxParameter(const TxID& txID, wallet::TxParameterID paramID, const ByteBuffer& blob, bool shouldNotifyAboutChanges)
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
                if (shouldNotifyAboutChanges)
                {
                    auto tx = getTx(txID);
                    if (tx.is_initialized())
                    {
                        notifyTransactionChanged(ChangeAction::Updated, { *tx });
                    }
                }
                return true;
            }
        }
        
        sqlite::Statement stm(_db, "INSERT INTO " TX_PARAMS_NAME " (" ENUM_TX_PARAMS_FIELDS(LIST, COMMA, ) ") VALUES(" ENUM_TX_PARAMS_FIELDS(BIND_LIST, COMMA, ) ");");
        TxParameter parameter;
        parameter.m_txID = txID;
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
            int colIdx = 0;
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

        bool setTxParameter(IWalletDB::Ptr db, const TxID& txID, TxParameterID paramID,
            const ECC::Point::Native& value, bool shouldNotifyAboutChanges)
        {
            ECC::Point pt;
            if (value.Export(pt))
            {
                return setTxParameter(db, txID, paramID, pt, shouldNotifyAboutChanges);
            }
            return false;
        }

        bool setTxParameter(IWalletDB::Ptr db, const TxID& txID, TxParameterID paramID,
            const ECC::Scalar::Native& value, bool shouldNotifyAboutChanges)
        {
            ECC::Scalar s;
            value.Export(s);
            return setTxParameter(db, txID, paramID, s, shouldNotifyAboutChanges);
        }

        bool setTxParameter(IWalletDB::Ptr db, const TxID& txID, TxParameterID paramID,
            const ByteBuffer& value, bool shouldNotifyAboutChanges)
        {
            return db->setTxParameter(txID, paramID, value, shouldNotifyAboutChanges);
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

                if (c.m_status == Coin::Available
                    && lockHeight <= currentHeight)
                {
                    total += c.m_ID.m_Value;
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
                    && c.m_ID.m_Type == keyType
                    && lockHeight <= currentHeight)
                {
                    total += c.m_ID.m_Value;
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
                    total += c.m_ID.m_Value;
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
                if (c.m_status == status && c.m_ID.m_Type == keyType)
                {
                    total += c.m_ID.m_Value;
                }
                return true;
            });
            return total;
        }

        WalletAddress createAddress(beam::IWalletDB::Ptr walletDB)
        {
            WalletAddress newAddress;
            newAddress.m_createTime = beam::getTimestamp();
            newAddress.m_OwnID = walletDB->AllocateKidRange(1);

			ECC::Scalar::Native sk;
			walletDB->get_MasterKdf()->DeriveKey(sk, Key::ID(newAddress.m_OwnID, Key::Type::Bbs));

            proto::Sk2Pk(newAddress.m_walletID.m_Pk, sk);

            BbsChannel ch;
            if (!walletDB->GetLastChannel(ch))
                ch = (BbsChannel)newAddress.m_walletID.m_Pk.m_pData[0] >> 3; // fallback

            newAddress.m_walletID.m_Channel = ch;
            return newAddress;
        }
    }
}
