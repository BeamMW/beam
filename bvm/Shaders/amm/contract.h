#pragma once
#include "../Math.h"
#include "../mintor/contract.h"
#include "../upgradable3/contract.h"

namespace Amm
{
    static const ShaderID s_pSID[] = {
        { 0x6e,0x1d,0x85,0xb3,0xdd,0x54,0xfc,0xfa,0x05,0xd8,0x54,0x80,0x19,0x23,0x23,0xe1,0x34,0xdb,0x98,0x65,0x21,0x62,0xcf,0x3b,0xbd,0x4b,0x9c,0x52,0x34,0x36,0xf4,0xff },
    };

#pragma pack (push, 1)

    using MultiPrecision::Float;

    struct Tags
    {
        // don't use tag=1 for multiple data entries, it's used by Upgradable2
        static const uint8_t s_Pool = 2;
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

            // Ensure the liquiduty is added according to the current proportion: da/db == a/b, or a*db == b*da
            //
            // due to round-off errors we allow slight deviations.
            // if da/db > a/b, then da/(db+1) <= (a+1)/b, and vice-versa. Means:
            //
            // b*da <= (a+1)*(db+1) = a*db + (a + db + 1)
            // a*db <= (b+1)*(da+1) = b*da + (b + da + 1)

            auto adb = MultiPrecision::From(m_Tok1) * MultiPrecision::From(d.m_Tok2);
            auto bda = MultiPrecision::From(m_Tok2) * MultiPrecision::From(d.m_Tok1);

            int n = adb.cmp(bda);
            if (n)
            {
                if (n > 0)
                {
                    // a*db > b*da, da/db < a/b
                    if (adb > bda + MultiPrecision::From(m_Tok2 + d.m_Tok1 + 1)) // don't care about overflow
                        return -1; // da/db too small
                }
                else
                {
                    // b*da > a*db, da/db > a/b
                    if (bda > adb + MultiPrecision::From(m_Tok1 + d.m_Tok2 + 1))
                        return 1; // da/db too large
                }
            }

            return 0; // da/db is in range
        }

        static Amount ToAmount(const Float& f)
        {
            static_assert(sizeof(Amount) == sizeof(f.m_Num));
            Env::Halt_if(f.m_Order > 0); // would overflow
            return f.Get();
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
                dRet.m_Tok2 = Float(m_Tok1) * kRet;
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

        Amount Trade(Amount vBuy1)
        {
            Float vol = Float(m_Tok1) * Float(m_Tok2);

            Env::Halt_if(m_Tok1 <= vBuy1);
            m_Tok1 -= vBuy1;

            Amount valPay = ToAmount(vol / Float(m_Tok1));
            Strict::Sub(valPay, m_Tok2);

            // add comission 0.3%, plus add 1 groth (min unit) to compensate for potential round-off error during division
            Amount fee = valPay / 1000 * 3 + 1;
            Strict::Add(valPay, fee);
            Strict::Add(m_Tok2, valPay);

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

            static const AssetID s_Token = 0x80000000;
        };

        struct Key
        {
            uint8_t m_Tag = Tags::s_Pool;
            ID m_ID;
        };

        Mintor::Token::ID m_tidCtl;
        Totals m_Totals;
    };

    namespace Method
    {
        struct Create
        {
            static const uint32_t s_iMethod = 0;
            Upgradable3::Settings m_Upgradable;
        };

        struct PoolUserInvoke
        {
            Pool::ID m_Pid;
            PubKey m_pk;
        };

        struct AddLiquidity :public PoolUserInvoke
        {
            static const uint32_t s_iMethod = 3;
            Amounts m_Amounts;
        };

        struct Withdraw :public PoolUserInvoke
        {
            static const uint32_t s_iMethod = 4;
            Amount m_Ctl;
        };

        struct Trade :public PoolUserInvoke
        {
            static const uint32_t s_iMethod = 5;
            Amount m_Buy1;
        };
    }
#pragma pack (pop)

} // namespace Amm
