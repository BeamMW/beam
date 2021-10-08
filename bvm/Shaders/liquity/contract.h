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
        static const uint8_t s_Epoch_Stable = 2;
    };

    typedef ExchangePool::Pair Pair; // s == stable, b == collateral

    struct Balance
    {
        struct Key {
            uint8_t m_Tag = 1;
            PubKey m_Pk;
            AssetID m_Aid;
        };

        typedef Amount ValueType;
    };

    struct EpochKey {
        uint8_t m_Tag;
        uint32_t m_iEpoch;
    };

    struct Trove
    {
        struct Key
        {
            uint8_t m_Tag = Tags::s_Trove;
            uint32_t m_iTrove;
        };

        PubKey m_pkOwner;
        Pair m_Amounts;
        ExchangePool::User m_RedistUser; // accumulates enforced liquidations
    };

    struct Global
    {
        ContractID m_cidOracle;
        AssetID m_Aid;
        MultiPrecision::Float m_kTokenPrice;

        struct Troves
        {
            uint32_t m_iLast;
            Pair m_Totals; // Total minted tokens and collateral in all troves

        } m_Troves;

        struct RedistPool
            :public DistributionPool
        {
            bool Add(Trove& t)
            {
                assert(!t.m_RedistUser.m_iEpoch);

                if (!t.m_Amounts.s)
                    return false;

                UserAdd(t.m_RedistUser, t.m_Amounts.s);
                assert(.m_RedistUser.m_iEpoch);

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
                    m_Active.m_Balance.s -= t.m_Amounts.s; // silent removal
                else
                {
                    Pair out;
                    UserDel(t.m_RedistUser, out, stor);

                    t.m_Amounts.s = out.s;
                    t.m_Amounts.b += out.b;
                }

                t.m_RedistUser.m_iEpoch = 0;

                return true;
            }

            bool Liquidate(Trove& t)
            {
                assert(!t.m_RedistUser.m_iEpoch); // should not be a part of the pool during liquidation!

                if (t.m_Amounts.s > get_TotalSell())
                    return false;

                Trade(t.m_Amounts);

                t.m_Amounts.s = 0;
                t.m_Amounts.b = 0;
                return true;
            }

        } m_RedistPool;

        struct StabilityPool
            :public ExchangePool
        {
            bool Liquidate(Trove& t)
            {
                auto maxVal = get_TotalSell();
                if (!maxVal)
                    return false;

                if (maxVal >= t.m_Amounts.s)
                {
                    Trade(t.m_Amounts);

                    t.m_Amounts.s = 0;
                    t.m_Amounts.b = 0;
                }
                else
                {
                    // partial liquidation is ok
                    Pair part = t.m_Amounts.get_Fraction(maxVal, t.m_Amounts.s);
                    if (!part.s)
                        return false;

                    Trade(part);

                    t.m_Amounts = t.m_Amounts - part;
                }

                return true;
            }

        } m_StabPool;

    };


    struct OpenTrove
    {
        static const uint32_t s_iMethod = 3;

        PubKey m_pkOwner;
        Pair m_Amounts;
    };

    struct CloseTrove
    {
        static const uint32_t s_iMethod = 4;
        uint32_t m_iTrove;
    };

#pragma pack (pop)

}
