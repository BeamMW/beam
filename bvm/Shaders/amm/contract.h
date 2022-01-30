#pragma once
#include "../Math.h"

namespace Amm
{
    static const ShaderID s_SID = { 0xe3,0x21,0x90,0x44,0x42,0x00,0x0f,0xfd,0x2d,0x2f,0xf0,0x46,0x3f,0x8a,0x77,0x21,0x1b,0x82,0xe4,0x61,0xd3,0x97,0xe1,0xb2,0x20,0x8c,0xc9,0x3b,0xb6,0xa1,0x04,0x65 };

#pragma pack (push, 1)

    using MultiPrecision::Float;

    struct Tags
    {
        // don't use tag=1 for multiple data entries, it's used by Upgradable2
        static const uint8_t s_Pool = 2;
        static const uint8_t s_User = 3;
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
            Amount ctl0 = m_Ctl;

            Strict::Sub(m_Ctl, dCtl);
            if (m_Ctl)
            {
                // round the return part to the samller side, this way we'll never have empty components
                Float kRet = Float(dCtl) / Float(m_Ctl);

                dRet.m_Tok1 = Float(m_Tok1) * kRet;
                dRet.m_Tok2 = Float(m_Tok1) * kRet;
            }
            else
                // last provider
                dRet = Cast::Down<Amounts>(*this);

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
        };

        struct Key
        {
            uint8_t m_Tag = Tags::s_Pool;
            ID m_ID;
        };

        Totals m_Totals;
    };

    struct User
    {
        struct ID
        {
            Pool::ID m_Pid;
            PubKey m_pk;
        };

        struct Key {
            uint8_t m_Tag = Tags::s_User;
            ID m_ID;
        };
    
        Amount m_Ctl;
    };

    namespace Method
    {
        struct PoolInvoke
        {
            Pool::ID m_Pid;
        };

        struct PoolUserInvoke
        {
            User::ID m_Uid;
        };

        struct AddLiquidity :public PoolUserInvoke
        {
            static const uint32_t s_iMethod = 2;
            Amounts m_Amounts;
        };

        struct Withdraw :public PoolUserInvoke
        {
            static const uint32_t s_iMethod = 3;
            Amount m_Ctl;
        };

        struct Trade :public PoolInvoke
        {
            static const uint32_t s_iMethod = 4;
            Amount m_Buy1;
        };
    }
#pragma pack (pop)

} // namespace Amm
