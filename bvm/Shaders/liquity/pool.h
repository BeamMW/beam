#pragma once
#include "../Math.h"

struct ExchangePool
{
    struct Decay
    {
        typedef MultiPrecision::UInt<2> Type;
        static const uint64_t s_One = 1ull << 60;
        static const uint64_t s_Threshold = 1ull << 40;
    };

    struct Sigma
    {
        // sigma grows by portions of dSigma = (delta_buy_amount * kDecay / total_sell_amount).
        // kDecay should not fall below s_Threshold^2, i.e. 1 << 20
        //
        // The normalization factor is 2 words. Means in worst case dSigma is no less than 2^20 (practically would be much much larger).
        // On the opposite side normalized dSigma may grow as 6 words (practically would be much lower).
        // Hence to mitigate possible overflow we allocate 7 words

        typedef MultiPrecision::UInt<7> Type;
        static const uint32_t s_NormWords = 2;
    };

    struct Pair {
        Amount s;
        Amount b;
    };

    struct Epoch
    {
        uint32_t m_Users;
        Pair m_Balance;
        Sigma::Type m_Sigma;
        Decay::Type m_kDecay;

        void Trade_(const Pair& d)
        {
            Amount s0 = m_Balance.s;
            assert(d.s <= s0);

            if (!s0)
            {
                assert(!d.s && !d.b);
                return;
            }

            auto s0_ = MultiPrecision::From(s0);

            {
                // dSigma = ((d.b * m_kDecay) << normalization) / s0

                auto val = MultiPrecision::From(d.b) * m_kDecay; // 4 words
                static_assert(val.nWords == 4);

                MultiPrecision::UInt<4 + Sigma::s_NormWords> valNorm, dSigma;
                valNorm.Assign<Sigma::s_NormWords>(val);
                dSigma.SetDivResid(valNorm, s0_);

                m_Sigma += dSigma; // overflow should not happen
            }

            Strict::Add(m_Balance.b, d.b);
            m_Balance.s -= d.s;

            {
                // m_kDecay *= m_Balance.s / s0;
                auto val = MultiPrecision::From(m_Balance.s) * m_kDecay;
                m_kDecay.SetDivResid(val, s0_);
            }
        }
    };

    Epoch m_Active;
    Epoch m_Draining;

    Amount get_TotalSell() const {
        // won't overflow, we test for overflow when user joins
        return m_Active.m_Balance.s + m_Draining.m_Balance.s;
    }

    uint32_t m_iActive;

    void Init()
    {
        _POD_(*this).SetZero();
        m_Active.m_kDecay = Decay::s_One;
    }

    struct User
    {
        uint32_t m_iEpoch;
        Sigma::Type m_Sigma0;

        static const uint32_t s_WeightNormWords = 3;
        typedef MultiPrecision::UInt<2 + s_WeightNormWords> WeightType;
        WeightType m_SellScaled;

        void Add_(Epoch& e, Amount valSell)
        {
            assert(valSell);
            m_Sigma0 = e.m_Sigma;

            assert(e.m_kDecay >= MultiPrecision::From(Decay::s_Threshold));

            {
                // u.m_SellScaled = valSell / m_Active.m_kDecay;
                MultiPrecision::UInt<2 + s_WeightNormWords> val;
                val.Assign<s_WeightNormWords>(MultiPrecision::From(valSell));

                m_SellScaled.SetDivResid(val, e.m_kDecay);
            }

            Strict::Add(e.m_Balance.s, valSell);
            e.m_Users++; // won't overflow, 4bln isn't feasible
        }


        void Del_(Epoch& e, Pair& out)
        {
            assert(e.m_Users);
            if (--e.m_Users)
            {
                out.s = get_AmountSaturated<s_WeightNormWords>(m_SellScaled * e.m_kDecay, e.m_Balance.s);

                Sigma::Type dS;
                dS.SetSub(e.m_Sigma, m_Sigma0);

                out.b = get_AmountSaturated<s_WeightNormWords + Sigma::s_NormWords>(m_SellScaled * dS, e.m_Balance.b);
            }
            else
                out = e.m_Balance;

            e.m_Balance.s -= out.s;
            e.m_Balance.b -= out.b;
        }
    };

    void UserAdd(User& u, Amount valSell)
    {
        u.m_iEpoch = m_iActive;
        u.Add_(m_Active, valSell);

        Env::Halt_if(get_TotalSell() < m_Active.m_Balance.s); // overflow test
    }

    template <class IO>
    void UserDel(User& u, Pair& out)
    {
        if (u.m_iEpoch == m_iActive)
        {
            u.Del_(m_Active, out);
            if (!m_Active.m_Users)
                ResetActive();
        }
        else
        {
            if (u.m_iEpoch + 1 == m_iActive)
                u.Del_(m_Draining, out);
            else
            {
                Epoch e;
                IO::Load(u.m_iEpoch, e);

                u.Del_(e, out);

                if (e.m_Users)
                    IO::Save(u.m_iEpoch, e);
                else
                    IO::Del(u.m_iEpoch);
            }
        }
    }

    void Trade(const Pair& d)
    {
        assert(d.s);

        // Active epoch must always be valid
        // Account for draining epoch iff not empty
        if (m_Draining.m_Users)
        {
            Amount totalSell = get_TotalSell();
            assert(d.s <= totalSell);

            // split among active/draining w.r.t. their currency proportion. Round in favor of draining
            Pair d0;
        
            d0.s = MulDiv(d.s, m_Active.m_Balance.s, totalSell);
            d0.b = MulDiv(d.b, m_Active.m_Balance.s, totalSell);

            m_Active.Trade_(d0);

            d0.s = d.s - d0.s;
            d0.b = d.b - d0.b;

            m_Draining.Trade_(d0);        }
        else
            m_Active.Trade_(d);
    }

    template <class IO>
    void OnPostTrade()
    {
        if (m_Active.m_kDecay >= Decay::s_Threshold)
            return;

        if (m_Draining.m_Users)
            IO::Save(m_iActive - 1, m_Draining);

        _POD_(m_Draining) = m_Active;

        ResetActive();
        m_iActive++;
    }

private:

    void ResetActive()
    {
        _POD_(m_Active).SetZero();
        m_Active.m_kDecay = Decay::s_One;
    }

    template <typename T1, typename T2>
    static T1 MulDiv(T1 x, T2 nom, T2 denom)
    {
        auto x_ = MultiPrecision::From(x);
        auto val = x_ * MultiPrecision::From(nom);
        x_.SetDivResid(val, MultiPrecision::From(denom));
        return x_.Get<0, T1>();
    }

    template <uint32_t nNormWords, uint32_t nWords>
    static Amount get_AmountSaturated(const MultiPrecision::UInt<nWords>& x, Amount bound)
    {
        if (!x.template IsZeroRange<2 + nNormWords, nWords>())
            return bound; // overflow

        auto val = x.template Get<nNormWords, Amount>();
        return std::min(val, bound);
    }

};

