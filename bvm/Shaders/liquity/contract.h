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
    };

    typedef ExchangePool::Pair Pair; // s == stable, b == collateral

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
        ID m_iRcrNext;
        ExchangePool::User m_RedistUser; // accumulates enforced liquidations

        MultiPrecision::Float get_Rcr() const
        {
            // rcr == raw collateralization ratio, i.e. b/s
            // Don't care about s==0 case (our Float doesn't handle inf), valid troves must always have s, otherwise they're closed
            return MultiPrecision::Float(m_Amounts.b) / MultiPrecision::Float(m_Amounts.s);
        }

        // minimum amount of tokens when opening or updating the trove. Normally should not go below this value.
        // Can decrease temporarily due to partial liquidation or redeeming, can happen only for the lowest trove.
        static const Amount s_MinAmountS = g_Beam2Groth * 10;

    };

    struct Global
    {
        ContractID m_cidOracle;
        AssetID m_Aid;
        MultiPrecision::Float m_kTokenPrice;

        struct Troves
        {
            Trove::ID m_iLastCreated;
            Trove::ID m_iRcrLow;
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

        static MultiPrecision::Float get_k150()
        {
            MultiPrecision::Float val = 3;
            val.m_Order--; // 3/2
            return val;
        }

        static MultiPrecision::Float get_k110()
        {
            MultiPrecision::Float val = 72090;
            val.m_Order -= 16; // 72090/2^16
            return val;
        }

        bool IsBelow150(MultiPrecision::Float rcr) const
        {
            return rcr < get_k150()* m_kTokenPrice;
        }

        bool IsBelow110(MultiPrecision::Float rcr) const
        {
            return rcr < get_k110()* m_kTokenPrice;
        }
    };

    struct FundsMove
    {
        Pair m_Amounts;
        uint8_t m_SpendS;
        uint8_t m_SpendB;
    };


    namespace Method
    {
        struct Create
        {
            static const uint32_t s_iMethod = 2;
            ContractID m_cidOracle;
        };

        struct OpenTrove
        {
            static const uint32_t s_iMethod = 3;

            PubKey m_pkOwner;
            Pair m_Amounts;
            Trove::ID m_iRcrPos1;
        };

        struct CloseTrove
        {
            static const uint32_t s_iMethod = 4;
            Trove::ID m_iTrove;
            Trove::ID m_iRcrPos0;

            FundsMove m_Fm;
        };

        struct FundsAccess
        {
            static const uint32_t s_iMethod = 5;
            PubKey m_pkUser;
            FundsMove m_Fm;
        };

        struct ModifyTrove
        {
            static const uint32_t s_iMethod = 6;
            Trove::ID m_iTrove;
            Trove::ID m_iRcrPos0;
            Trove::ID m_iRcrPos1;

            FundsMove m_Fm;
            FundsMove m_FmTrove;
        };

    } // namespace Method
#pragma pack (pop)

}
