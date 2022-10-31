#pragma once
#include "../Math.h"
#include "../upgradable3/contract.h"

namespace Amm
{
    static const ShaderID s_pSID[] = {
        { 0xda,0x6c,0x94,0x6c,0xec,0x3c,0x43,0x82,0x1a,0x6b,0x8a,0x57,0xf4,0xc9,0x71,0x1e,0xd7,0x38,0x32,0x8b,0xb2,0xe7,0x54,0xba,0x80,0xdc,0xbe,0x36,0x1a,0xb6,0xf8,0x6d },
    };

#pragma pack (push, 1)

    using MultiPrecision::Float;

    struct Tags
    {
        static const uint8_t s_Settings = 0;
        static const uint8_t s_Pool = 1;
    };

    struct Settings
    {
        ContractID m_cidDaoVault;
    };

    struct Amounts
    {
        Amount m_Tok1;
        Amount m_Tok2;

        void Swap()
        {
            std::swap(m_Tok1, m_Tok2);
        }
    };

    struct TradeRes
    {
        Amount m_PayPool; // goes to the pool, including trading fees
        Amount m_DaoFee; // dao fees
    };

    struct FeeSettings
    {
        uint8_t m_Kind;
        static const uint8_t s_Kinds = 3;

        // valPay gets the trade fees added (that are added to the pool), retval is the dao fees
        void Get(TradeRes& res, Amount valPay) const
        {
            switch (m_Kind)
            {
            case 0: // low volatility, fee is 0.05%
                res.m_PayPool = valPay / 2000;
                break;

            case 1: // mid volatility, fee is 0.3%
                res.m_PayPool = valPay / 1000 * 3; // multiply after division, avoid overflow risk
                break;

            default: // high volatility, fee is 1%
                res.m_PayPool = valPay / 100;
            }

            res.m_PayPool++; // add 1 groth (min unit) to compensate for potential round-off error during trade value calculation

            res.m_DaoFee = res.m_PayPool * 3 / 10; // 30% of the fees goe to dao. Won't overflow
            res.m_PayPool -= res.m_DaoFee;
            Strict::Add(res.m_PayPool, valPay);
        }
    };

    struct Totals :public Amounts
    {
        Amount m_Ctl;

        void AssertValid() const
        {
            assert(m_Ctl && m_Tok1 && m_Tok2);
        }

        int TestAdd(const Amounts& d) const
        {
            AssertValid();

            // Ensure the liquidity is added according to the current proportion: da/db == a/b, or a*db == b*da
            //
            // due to round-off errors we allow slight deviations.
            // if da/db > a/b, then da/(db+1) < a/b, and vice-versa. Means:
            //
            // b*da < a*(db+1) = a*db + a
            // a*db < b*(da+1) = b*da + b

            auto a = MultiPrecision::From(m_Tok1);
            auto b = MultiPrecision::From(m_Tok2);

            auto adb = a * MultiPrecision::From(d.m_Tok2);
            auto bda = b * MultiPrecision::From(d.m_Tok1);

            int n = adb.cmp(bda);
            if (n)
            {
                if (n > 0)
                {
                    // a*db > b*da, da/db < a/b
                    if (adb >= bda + b) // don't care about overflow
                        return -1; // da/db too small
                }
                else
                {
                    // b*da > a*db, da/db > a/b
                    if (bda >= adb + a)
                        return 1; // da/db too large
                }
            }

            return 0; // da/db is in range
        }

        void AddInitial(const Amounts& d)
        {
            Cast::Down<Amounts>(*this) = d;
            assert(m_Tok1 && m_Tok2);

            // select roughly geometric mean ( (tok1 * tok2) ^ (1/2) ) for the initial Ctl value
            uint32_t n1 = BitUtils::FindHiBit(m_Tok1);
            uint32_t n2 = BitUtils::FindHiBit(m_Tok2);

            
            if (m_Tok1 >= m_Tok2)
                m_Ctl = m_Tok1;
            else
            {
                m_Ctl = m_Tok2;
                std::swap(n1, n2);
            }

            m_Ctl >>= ((n1 - n2) >> 1);
            assert(m_Ctl);
        }

        static Amount ToAmount(const Float& f)
        {
            Amount ret;
            Env::Halt_if(!f.RoundDown(ret)); // don't allow overflow
            return ret;
        }

        void Add(const Amounts& d)
        {
            AssertValid();

            Amounts v = *this;
            Strict::Add(m_Tok1, d.m_Tok1);
            Strict::Add(m_Tok2, d.m_Tok2);

            // since da/db can be (slightly) different from a/b, we take the average of the grow factor
            // perhaps geometric average would be more precise, but never mind

            Float kGrow1 = Float(m_Tok1) / Float(v.m_Tok1);
            Float kGrow2 = Float(m_Tok2) / Float(v.m_Tok2);

            Float kGrow = kGrow1 + kGrow2;
            kGrow.m_Order--; // /= 2

            m_Ctl = ToAmount(Float(m_Ctl) * kGrow);
        }

        Amounts Remove(Amount dCtl)
        {
            AssertValid();

            Amounts dRet;

            if (dCtl < m_Ctl)
            {
                // round the return part to the samller side, this way we'll never have empty components
                Float kRet = Float(dCtl) / Float(m_Ctl);

                dRet.m_Tok1 = Float(m_Tok1) * kRet;
                dRet.m_Tok2 = Float(m_Tok2) * kRet;
            }
            else
            {
                Env::Halt_if(dCtl != m_Ctl);
                // last provider
                dRet = Cast::Down<Amounts>(*this);
            }

            m_Ctl -= dCtl;
            m_Tok1 -= dRet.m_Tok1;
            m_Tok2 -= dRet.m_Tok2;

            if (m_Ctl)
                AssertValid();

            return dRet;
        }

        Amount Trade(TradeRes& res, Amount vBuy1, const FeeSettings& fs) // retval is the raw price (without fees)
        {
            Float vol = Float(m_Tok1) * Float(m_Tok2);

            Env::Halt_if(m_Tok1 <= vBuy1);
            m_Tok1 -= vBuy1;

            Amount valPay = ToAmount(vol / Float(m_Tok1));
            Strict::Sub(valPay, m_Tok2);

            fs.Get(res, valPay);
            Strict::Add(m_Tok2, res.m_PayPool);

            return valPay;
        }
    };

    struct Pool
    {
        struct ID
        {
            AssetID m_Aid1;
            AssetID m_Aid2;
            // must be well-ordered
            FeeSettings m_Fees;
        };

        struct Key
        {
            uint8_t m_Tag = Tags::s_Pool;
            ID m_ID;
        };

        Totals m_Totals;
        AssetID m_aidCtl;
        PubKey m_pkCreator;
    };

    namespace Method
    {
        struct Create
        {
            static const uint32_t s_iMethod = 0;
            Upgradable3::Settings m_Upgradable;
            Settings m_Settings;
        };

        struct PoolInvoke {
            Pool::ID m_Pid;
        };

        struct PoolCreate :public PoolInvoke
        {
            static const uint32_t s_iMethod = 3;
            PubKey m_pkCreator;
        };

        struct PoolDestroy :public PoolInvoke
        {
            static const uint32_t s_iMethod = 4;
        };

        struct AddLiquidity :public PoolInvoke
        {
            static const uint32_t s_iMethod = 5;
            Amounts m_Amounts;
        };

        struct Withdraw :public PoolInvoke
        {
            static const uint32_t s_iMethod = 6;
            Amount m_Ctl;
        };

        struct Trade :public PoolInvoke
        {
            static const uint32_t s_iMethod = 7;
            Amount m_Buy1;
        };
    }
#pragma pack (pop)

} // namespace Amm
