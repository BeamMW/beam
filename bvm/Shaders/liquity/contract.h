#pragma once
#include "pool.h"

namespace Liquity
{
    static const ShaderID s_SID = { 0x15,0xac,0x21,0x69,0xa6,0x40,0x1a,0x2a,0x3c,0x27,0xed,0xcf,0xb5,0x49,0x6c,0xae,0xcd,0x3e,0x91,0xe9,0x48,0x7b,0xa6,0xe2,0x87,0xd0,0x1c,0x9d,0xe4,0x35,0xe7,0x66 };

#pragma pack (push, 1)

    struct Tags
    {
        static const uint8_t s_State = 0;
        static const uint8_t s_Trove = 1;
        static const uint8_t s_Epoch_Redist = 2;
        static const uint8_t s_Epoch_Stable = 3;
        static const uint8_t s_Balance = 4;
        static const uint8_t s_StabPool = 5;
        static const uint8_t s_ProfitPool = 5;
    };

    typedef MultiPrecision::Float Float;

    template <typename T> struct Pair_T
    {
        T Tok;
        T Col;
    };

    typedef Pair_T<Amount> Pair;

    struct Flow
    {
        Amount m_Val;
        uint8_t m_Spend; // not necessarily normalized, i.e. can be zero or any nnz value when comes from user

        void operator += (const Flow& c) {
            Add(c.m_Val, c.m_Spend);
        }

        void operator -= (const Flow& c) {
            Add(c.m_Val, !c.m_Spend);
        }

        void Add(Amount x, uint8_t bSpend)
        {
            if ((!m_Spend) == (!bSpend))
                Strict::Add(m_Val, x);
            else
            {
                if (m_Val >= x)
                    m_Val -= x;
                else
                {
                    m_Val = x - m_Val;
                    m_Spend = !m_Spend;
                }
            }
        }
    };

    typedef Pair_T<Flow> FlowPair;

    typedef StaticPool<Amount, Amount, 2> ProfitPool;

    struct Balance
    {
        struct Key {
            uint8_t m_Tag = Tags::s_Balance;
            PubKey m_Pk;
        };

        Pair m_Amounts;
    };

    struct EpochKey {
        uint8_t m_Tag;
        uint32_t m_iEpoch;
    };

    struct StabPoolEntry
    {
        struct Key
        {
            uint8_t m_Tag = Tags::s_StabPool;
            PubKey m_pkUser;
        };

        ExchangePool::User m_User;
    };

    struct ProfitPoolEntry
    {
        struct Key
        {
            uint8_t m_Tag = Tags::s_ProfitPool;
            PubKey m_pkUser;
        };

        ProfitPool::User m_User;
    };

    struct Trove
    {
        typedef uint32_t ID;

        struct Key
        {
            uint8_t m_Tag = Tags::s_Trove;
            ID m_iTrove;
        };

        PubKey m_pkOwner;
        Pair m_Amounts;
        ExchangePool::User m_RedistUser; // accumulates enforced liquidations

        Float get_Rcr() const
        {
            // rcr == raw collateralization ratio, i.e. Tok/Col
            // Don't care about s==0 case (our Float doesn't handle inf), valid troves must always have s, otherwise they're closed
            return Float(m_Amounts.Col) / Float(m_Amounts.Tok);
        }

        // minimum amount of tokens when opening or updating the trove. Normally should not go below this value.
        // Can decrease temporarily due to partial liquidation or redeeming, can happen only for the lowest trove.
        static const Amount s_MinAmountS = g_Beam2Groth * 10;
    };

    struct Settings
    {
        ContractID m_cidOracle;
        Amount m_TroveMinDebt; // minimum amount of tokens in an active trove. Can go below during forced update, i.e. partial liquidation
        Amount m_TroveLiquidationReserve;
        AssetID m_AidProfit;
    };

    struct Global
    {
        Settings m_Settings;
        AssetID m_Aid;

        struct Troves
        {
            Trove::ID m_iLastCreated;
            Pair m_Totals; // Total debt (== minted tokens) and collateral in all troves

            Float get_Trcr() const
            {
                return Float(m_Totals.Col) / Float(m_Totals.Tok);
            }

        } m_Troves;

        struct RedistPool
            :public DistributionPool
        {
            bool Add(Trove& t)
            {
                assert(!t.m_RedistUser.m_iEpoch);

                if (!t.m_Amounts.Tok)
                    return false;

                UserAdd(t.m_RedistUser, t.m_Amounts.Tok);
                assert(t.m_RedistUser.m_iEpoch);

                return true;
            }

            bool IsUnchanged(const Trove& t) const
            {
                return
                    (t.m_RedistUser.m_iEpoch == m_iActive) &&
                    (t.m_RedistUser.m_Sigma0 == m_Active.m_Sigma);
            }

            template <class Storage>
            bool Remove(Trove& t, Storage& stor)
            {
                if (!t.m_RedistUser.m_iEpoch)
                    return false;

                // try to avoid recalculations if nothing changed, to prevent inaccuracies
                //
                // // should not overflow, all values are bounded by totals.
                if (IsUnchanged(t))
                {
                    m_Active.m_Balance.s -= t.m_Amounts.Tok; // silent removal
                    m_Active.m_Users--;
                }
                else
                {
                    Pair out;
                    UserDel(t.m_RedistUser, out, stor);

                    t.m_Amounts.Tok = out.s;
                    t.m_Amounts.Col += out.b;
                }

                t.m_RedistUser.m_iEpoch = 0;

                return true;
            }

            bool Liquidate(Trove& t)
            {
                assert(!t.m_RedistUser.m_iEpoch); // should not be a part of the pool during liquidation!

                if (!get_TotalSell())
                    return false; // empty

                Pair p;
                p.s = t.m_Amounts.Tok;
                p.b = t.m_Amounts.Col;
                Trade(p);

                _POD_(t.m_Amounts).SetZero();
                return true;
            }

        } m_RedistPool;

        struct StabilityPool
            :public ExchangePool
        {
            bool LiquidatePartial(Trove& t)
            {
                Pair p;
                p.s = get_TotalSell();
                if (!p.s)
                    return false;

                if (p.s >= t.m_Amounts.Tok)
                {
                    p.s = t.m_Amounts.Tok;
                    p.b = t.m_Amounts.Col;
                    _POD_(t.m_Amounts).SetZero();
                }
                else
                {
                    p.b = Float(t.m_Amounts.Col) * Float(p.s) / Float(t.m_Amounts.Tok);
                    assert(p.b <= t.m_Amounts.Col);

                    t.m_Amounts.Tok -= p.s;
                    t.m_Amounts.Col -= p.b;
                }

                Trade(p);
                return true;
            }

        } m_StabPool;

        ProfitPool m_ProfitPool;

        struct Price
        {
            Float m_Value;

            static Float get_k150()
            {
                Float val = 3;
                val.m_Order--; // 3/2
                return val;
            }

            static Float get_k110()
            {
                Float val = 72090;
                val.m_Order -= 16; // 72090/2^16
                return val;
            }

            static Float get_k100()
            {
                return Float(1u);
            }

            bool IsBelow150(Float rcr) const
            {
                return rcr * m_Value < get_k150();
            }

            bool IsBelow110(Float rcr) const
            {
                return rcr * m_Value < get_k110();
            }

            bool IsBelow100(Float rcr) const
            {
                return rcr * m_Value < get_k100();
            }
        };

        Amount get_LiquidationRewardReduce(Float icr) const
        {
            Amount half = m_Settings.m_TroveLiquidationReserve / 2;

            // icr == 0 - full reward
            // icr == threshold - reward is reduced to half

            Amount ret = Float(half) * icr / Price::get_k110();

            return std::min(ret, half);
        }

        Float m_kBaseRate;
        void UpdateBaseRate()
        {
            // TODO
        }
    };

    namespace Method
    {
        struct OracleGet
        {
            static const uint32_t s_iMethod = 3;
            Float m_Val;
        };

        struct Create
        {
            static const uint32_t s_iMethod = 2;
            Settings m_Settings;
        };

        struct BaseTx {
            FlowPair m_Flow;
        };

        struct BaseTxUser :public BaseTx {
            PubKey m_pkUser;
        };

        struct BaseTxTrove :public BaseTx {
            Trove::ID m_iTrove;
        };

        struct TroveOpen :public BaseTxUser
        {
            static const uint32_t s_iMethod = 3;
            Pair m_Amounts;
        };

        struct TroveClose :public BaseTxTrove
        {
            static const uint32_t s_iMethod = 4;
        };

        struct TroveModify :public BaseTxTrove
        {
            static const uint32_t s_iMethod = 5;
            Pair m_Amounts;
        };

        struct FundsAccess :public BaseTxUser
        {
            static const uint32_t s_iMethod = 6;
        };

        struct UpdStabPool :public BaseTxUser
        {
            static const uint32_t s_iMethod = 7;
            Amount m_NewAmount;
        };

        struct EnforceLiquidatation :public BaseTxUser
        {
            static const uint32_t s_iMethod = 8;
            uint32_t m_Count;
            // followed by array of Trove::ID
        };

        struct UpdProfitPool :public BaseTxUser
        {
            static const uint32_t s_iMethod = 9;
            Amount m_NewAmount;
        };

    } // namespace Method
#pragma pack (pop)

}
